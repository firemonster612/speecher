// Portal requests, the screenshot provider and the portal's Global Shortcut
// binder against a fake desktop portal on the suite's private session bus.
#include "common/test_suites.h"

#include "core/SettingsStore.h"
#include "platform/PortalGlobalShortcutBinder.h"
#include "platform/PortalRequest.h"
#include "platform/PortalScreenshotContextProvider.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace speecher;

namespace {

constexpr auto shortcutInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto screenshotInterface = "org.freedesktop.portal.Screenshot";

// The desktop portal, on a connection of its own. It answers the version
// query and Close itself and holds every other call until the test answers.
class FakePortal final : public QDBusVirtualObject {
public:
    FakePortal()
        : m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                              QStringLiteral("speecher-fake-portal")))
    {
    }

    ~FakePortal() override { QDBusConnection::disconnectFromBus(m_bus.name()); }

    bool start()
    {
        return m_bus.registerService(QString::fromLatin1(portalService))
            && m_bus.registerVirtualObject(QString::fromLatin1(portalPath), this,
                                           QDBusConnection::SubPath);
    }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        if (message.member() == QLatin1String("Get")) {
            m_bus.send(message.createReply(QVariant::fromValue(QDBusVariant(1U))));
            return true;
        }
        if (message.member() == QLatin1String("Close")) {
            m_bus.send(message.createReply());
        }
        m_calls.append(message);
        return true;
    }

    void clear() { m_calls.clear(); }

    // Waits for the oldest untaken call to `method` and takes it; an empty
    // message when none arrives.
    QDBusMessage take(const QString &method)
    {
        QDBusMessage call;
        const bool arrived = QTest::qWaitFor([&] {
            for (qsizetype i = 0; i < m_calls.size(); ++i) {
                if (m_calls.at(i).member() == method) {
                    call = m_calls.takeAt(i);
                    return true;
                }
            }
            return false;
        });
        return arrived ? call : QDBusMessage();
    }

    bool received(const QString &method) const
    {
        return std::any_of(m_calls.cbegin(), m_calls.cend(),
                           [&](const QDBusMessage &call) { return call.member() == method; });
    }

    // Two round trips from Speecher's connection: the first returns once
    // Speecher has handled what the portal sent before it, the second once
    // the portal holds every call Speecher made in response.
    void sync()
    {
        for (int trip = 0; trip < 2; ++trip) {
            QDBusMessage get = QDBusMessage::createMethodCall(
                QString::fromLatin1(portalService), QString::fromLatin1(portalPath),
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
            get << QString::fromLatin1(shortcutInterface) << QStringLiteral("version");
            // finished, unlike isFinished(), is queued behind the replies
            // that came before it.
            QDBusPendingCallWatcher watcher(QDBusConnection::sessionBus().asyncCall(get));
            QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
            QVERIFY(QTest::qWaitFor([&] { return !finished.isEmpty(); }));
        }
    }

    // The handle portal 0.9 and later give the request `call` opens.
    static QString requestPath(const QDBusMessage &call)
    {
        const QString sender = call.service().mid(1).replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString token = qdbus_cast<QVariantMap>(call.arguments().constLast())
                                  .value(QStringLiteral("handle_token")).toString();
        return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    }

    void reply(const QDBusMessage &call, const QVariantList &arguments = {})
    {
        m_bus.send(call.createReply(arguments));
    }

    void respond(const QString &requestPath, uint response, const QVariantMap &results = {})
    {
        QDBusMessage signal = QDBusMessage::createSignal(
            requestPath, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
        signal << response << results;
        m_bus.send(signal);
    }

    // Replies to `call` with its request's handle, then ends the request.
    void answer(const QDBusMessage &call, uint response, const QVariantMap &results = {})
    {
        // take() found no call; the test's own checks report what's missing.
        if (call.type() != QDBusMessage::MethodCallMessage) {
            return;
        }
        const QString path = requestPath(call);
        reply(call, {QVariant::fromValue(QDBusObjectPath(path))});
        respond(path, response, results);
    }

    // The desktop's keys press and release a bound shortcut; `timestamp` is
    // the portal's, in milliseconds, or zero for a backend that sends none.
    void activate(const QString &session, const QString &shortcutId, qulonglong timestamp = 0)
    {
        sendShortcutSignal(QStringLiteral("Activated"), session, shortcutId, timestamp);
    }

    void deactivate(const QString &session, const QString &shortcutId, qulonglong timestamp = 0)
    {
        sendShortcutSignal(QStringLiteral("Deactivated"), session, shortcutId, timestamp);
    }

private:
    void sendShortcutSignal(const QString &name, const QString &session, const QString &shortcutId,
                            qulonglong timestamp)
    {
        QDBusMessage signal = QDBusMessage::createSignal(
            QString::fromLatin1(portalPath), QString::fromLatin1(shortcutInterface), name);
        signal << QVariant::fromValue(QDBusObjectPath(session)) << shortcutId << timestamp << QVariantMap();
        m_bus.send(signal);
    }

    QDBusConnection m_bus;
    QList<QDBusMessage> m_calls;
};

QVariantMap sessionResults(const QString &session)
{
    return {{QStringLiteral("session_handle"), session}};
}

QVariantMap shortcutResults(const QString &shortcutId, const QString &trigger)
{
    PortalShortcut shortcut;
    shortcut.id = shortcutId;
    shortcut.properties.insert(QStringLiteral("trigger_description"), trigger);
    return {{QStringLiteral("shortcuts"), QVariant::fromValue(PortalShortcuts{shortcut})}};
}

} // namespace

class PortalTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY2(m_portal.start(), "The fake portal needs a session bus of its own");
        // The binder offers the portal only to Wayland sessions.
        qputenv("XDG_SESSION_TYPE", "wayland");
    }

    void init()
    {
        m_portal.clear();
        SettingsStore().setPortalShortcutCleared(GlobalShortcutRole::Dictation, false);
    }

    // A portal older than 0.9 picks the request's path itself and can send
    // its Response before the reply that names the path.
    void requestTakesAResponseThatArrivesBeforeItsHandle()
    {
        auto *request = new PortalRequest(this);
        QSignalSpy answered(request, &PortalRequest::answered);
        request->send(QString::fromLatin1(screenshotInterface), QStringLiteral("Screenshot"),
                      {QString(), QVariantMap()}, 5000);

        const QDBusMessage call = m_portal.take(QStringLiteral("Screenshot"));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/old/1");
        m_portal.respond(path, 0, {{QStringLiteral("uri"), QStringLiteral("file:///shot.png")}});
        m_portal.reply(call, {QVariant::fromValue(QDBusObjectPath(path))});

        QTRY_COMPARE(answered.count(), 1);
        QCOMPARE(answered.first().at(1).toMap().value(QStringLiteral("uri")).toString(),
                 QStringLiteral("file:///shot.png"));
    }

    // The deadline closes the request even when the portal names it only
    // afterwards, so no dialog outlives the request that asked for it.
    void requestPastItsDeadlineTimesOutAndIsClosed()
    {
        auto *request = new PortalRequest(this);
        QSignalSpy timedOut(request, &PortalRequest::timedOut);
        request->send(QString::fromLatin1(screenshotInterface), QStringLiteral("Screenshot"),
                      {QString(), QVariantMap()}, 50);

        const QDBusMessage call = m_portal.take(QStringLiteral("Screenshot"));
        QTRY_COMPARE(timedOut.count(), 1);
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/late/1");
        m_portal.reply(call, {QVariant::fromValue(QDBusObjectPath(path))});

        QCOMPARE(m_portal.take(QStringLiteral("Close")).path(), path);
    }

    void screenshotThePersonCancelsFailsTheCapture()
    {
        PortalScreenshotContextProvider screenshots;
        QSignalSpy failed(&screenshots, &ScreenshotContextProvider::failed);
        screenshots.capture();

        m_portal.answer(m_portal.take(QStringLiteral("Screenshot")), 1);

        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Screenshot capture was cancelled"));
    }

    // Choose shortcut registers Speecher's host identity, opens a session and
    // binds the shortcut there; the desktop's keys then press it.
    void choosingAShortcutBindsItInANewSession()
    {
        SettingsStore settings;
        PortalGlobalShortcutBinder binder(&settings);
        QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);

        chooseShortcut(binder, QStringLiteral("/session/1"), QStringLiteral("Ctrl+Alt+D"));
        m_portal.activate(QStringLiteral("/session/1"), binder.action().id);

        QTRY_COMPARE(activated.count(), 1);
    }

    // Removing the shortcut while the host identity is still registering
    // must not let that registration go on to open the chooser, and the
    // removal holds when Speecher next starts.
    void removalWhileTheIdentityRegistersOpensNoSession()
    {
        SettingsStore settings;
        PortalGlobalShortcutBinder binder(&settings);
        QTRY_VERIFY(binder.supportKnown());
        binder.registerShortcut();
        const QDBusMessage registration = m_portal.take(QStringLiteral("Register"));
        QVERIFY(registration.type() == QDBusMessage::MethodCallMessage);

        QVERIFY(binder.removeRegistration());
        m_portal.reply(registration);
        m_portal.sync();
        QVERIFY(!m_portal.received(QStringLiteral("CreateSession")));

        PortalGlobalShortcutBinder relaunched(&settings);
        QTRY_VERIFY(relaunched.supportKnown());
        relaunched.bind();
        m_portal.sync();
        QVERIFY(!m_portal.received(QStringLiteral("Register")));
        QVERIFY(!m_portal.received(QStringLiteral("CreateSession")));
    }

    // Cancelling the chooser for a new shortcut keeps the one already bound:
    // the binder restores it from the portal in a fresh session.
    void cancelledChooserRestoresTheBoundShortcut()
    {
        SettingsStore settings;
        PortalGlobalShortcutBinder binder(&settings);
        chooseShortcut(binder, QStringLiteral("/session/1"), QStringLiteral("Ctrl+Alt+D"));
        QSignalSpy finished(&binder, &GlobalShortcutBinder::registrationFinished);
        QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);

        binder.registerShortcut();
        m_portal.answer(m_portal.take(QStringLiteral("CreateSession")), 0,
                        sessionResults(QStringLiteral("/session/2")));
        m_portal.answer(m_portal.take(QStringLiteral("BindShortcuts")), 1);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("Setup was cancelled. Try again."));

        m_portal.answer(m_portal.take(QStringLiteral("CreateSession")), 0,
                        sessionResults(QStringLiteral("/session/3")));
        m_portal.answer(m_portal.take(QStringLiteral("ListShortcuts")), 0,
                        shortcutResults(binder.action().id, QStringLiteral("Ctrl+Alt+D")));
        m_portal.activate(QStringLiteral("/session/3"), binder.action().id);

        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(binder.shortcutDisplay(), QStringLiteral("Ctrl+Alt+D"));
        // The portal names its keys only in words. A stand-in combination
        // would reach the overlap check between shortcuts and the Cancel
        // Shortcut's label in a review.
        QVERIFY(binder.shortcut().isEmpty());
    }

    // The portal stamps Activated and Deactivated itself, so a release handled
    // late still reports the physical hold, measured from the first press
    // when the key repeats. A backend that sends no timestamp leaves the
    // controller to time it.
    void releaseReportsThePhysicalHold()
    {
        SettingsStore settings;
        PortalGlobalShortcutBinder binder(&settings);
        chooseShortcut(binder, QStringLiteral("/session/1"), QStringLiteral("Ctrl+Alt+D"));
        QSignalSpy released(&binder, &GlobalShortcutBinder::deactivated);

        m_portal.activate(QStringLiteral("/session/1"), binder.action().id, 5000);
        m_portal.activate(QStringLiteral("/session/1"), binder.action().id, 5040);
        m_portal.deactivate(QStringLiteral("/session/1"), binder.action().id, 5080);
        QTRY_COMPARE(released.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), 80);

        m_portal.activate(QStringLiteral("/session/1"), binder.action().id);
        m_portal.deactivate(QStringLiteral("/session/1"), binder.action().id);
        QTRY_COMPARE(released.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), -1);
    }

    // A session replaced or removed while its key is held never sends that
    // release, and the press must not stretch the next hold.
    void holdEndsWithItsSession()
    {
        SettingsStore settings;
        PortalGlobalShortcutBinder binder(&settings);
        chooseShortcut(binder, QStringLiteral("/session/1"), QStringLiteral("Ctrl+Alt+D"));
        QSignalSpy released(&binder, &GlobalShortcutBinder::deactivated);

        m_portal.activate(QStringLiteral("/session/1"), binder.action().id, 5000);
        m_portal.sync();
        binder.registerShortcut();
        bindInNewSession(binder, QStringLiteral("/session/2"), QStringLiteral("Ctrl+Alt+D"));
        m_portal.activate(QStringLiteral("/session/2"), binder.action().id, 20000);
        m_portal.deactivate(QStringLiteral("/session/2"), binder.action().id, 20080);
        QTRY_COMPARE(released.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), 80);

        m_portal.activate(QStringLiteral("/session/2"), binder.action().id, 30000);
        m_portal.sync();
        QVERIFY(binder.removeRegistration());
        binder.registerShortcut();
        bindInNewSession(binder, QStringLiteral("/session/3"), QStringLiteral("Ctrl+Alt+D"));
        m_portal.activate(QStringLiteral("/session/3"), binder.action().id, 40000);
        m_portal.deactivate(QStringLiteral("/session/3"), binder.action().id, 40080);
        QTRY_COMPARE(released.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), 80);
    }

private:
    // Takes a new binder through Choose shortcut, where the desktop assigns
    // `trigger` in `session`.
    void chooseShortcut(PortalGlobalShortcutBinder &binder, const QString &session, const QString &trigger)
    {
        QTRY_VERIFY(binder.supportKnown());
        binder.registerShortcut();
        m_portal.reply(m_portal.take(QStringLiteral("Register")));
        bindInNewSession(binder, session, trigger);
    }

    // Answers the chooser registerShortcut() opened: the desktop assigns
    // `trigger` in `session`.
    void bindInNewSession(PortalGlobalShortcutBinder &binder, const QString &session, const QString &trigger)
    {
        QSignalSpy finished(&binder, &GlobalShortcutBinder::registrationFinished);
        m_portal.answer(m_portal.take(QStringLiteral("CreateSession")), 0, sessionResults(session));
        m_portal.answer(m_portal.take(QStringLiteral("BindShortcuts")), 0,
                        shortcutResults(binder.action().id, trigger));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(binder.shortcutDisplay(), trigger);
    }

    FakePortal m_portal;
};

#include "test_portal.moc"

int runPortalTests(int argc, char **argv)
{
    PortalTests tests;
    return runTestSuite(&tests, argc, argv);
}
