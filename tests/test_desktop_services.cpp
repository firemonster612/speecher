// The KWin active-window probe and media pausing against a fake KWin and a
// fake media player on the suite's private session bus.
#include "common/test_suites.h"

#include "platform/KWinActiveWindow.h"
#include "platform/MediaPauseController.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

using namespace speecher;

namespace {

// A service on a connection of its own, answering calls to `path` and below.
class FakeService : public QDBusVirtualObject {
public:
    explicit FakeService(const QString &name)
        : m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, name))
    {
    }
    ~FakeService() override { QDBusConnection::disconnectFromBus(m_bus.name()); }

    bool start(const QString &service, const QString &path)
    {
        return m_bus.registerService(service)
            && m_bus.registerVirtualObject(path, this, QDBusConnection::SubPath);
    }

    QString introspect(const QString &) const override { return {}; }

protected:
    QDBusConnection m_bus;
};

// KWin's scripting interface. Running a loaded script calls back the probe it
// names with a fixed window. The probe blocks on its calls, so this answers
// from a thread of its own.
class FakeKWin final : public FakeService {
public:
    FakeKWin()
        : FakeService(QStringLiteral("speecher-fake-kwin"))
    {
        m_thread.start();
        moveToThread(&m_thread);
    }
    ~FakeKWin() override
    {
        QDBusConnection::disconnectFromBus(m_bus.name());
        m_thread.quit();
        m_thread.wait();
    }

    // A script already loaded under `plugin`, as a probe that never got to
    // unload leaves it.
    void leaveLoaded(const QString &plugin) { m_loaded.insert(plugin); }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        const QVariantList arguments = message.arguments();
        if (message.member() == QLatin1String("loadScript")) {
            // KWin turns down a plugin name it already has.
            const bool fresh = !m_loaded.contains(arguments.at(1).toString());
            m_loaded.insert(arguments.at(1).toString());
            m_scriptPath = arguments.at(0).toString();
            m_bus.send(message.createReply(fresh ? 1 : -1));
        } else if (message.member() == QLatin1String("unloadScript")) {
            m_bus.send(message.createReply(m_loaded.remove(arguments.at(0).toString())));
        } else if (message.member() == QLatin1String("run")) {
            m_bus.send(message.createReply());
            reportWindowTo(m_scriptPath);
        }
        return true;
    }

private:
    void reportWindowTo(const QString &scriptPath)
    {
        QFile script(scriptPath);
        if (!script.open(QIODevice::ReadOnly)) {
            return;
        }
        const QRegularExpressionMatch target =
            QRegularExpression(QStringLiteral("callDBus\\('([^']*)', '([^']*)', '([^']*)'"))
                .match(QString::fromUtf8(script.readAll()));
        QDBusMessage report = QDBusMessage::createMethodCall(
            target.captured(1), target.captured(2), target.captured(3), QStringLiteral("reportWindow"));
        report << QStringLiteral("konsole") << QStringLiteral("konsole") << QStringLiteral("Shell")
               << QStringLiteral("4242") << QStringLiteral("{window}");
        m_bus.send(report);
    }

    QThread m_thread;
    QSet<QString> m_loaded;
    QString m_scriptPath;
};

// An MPRIS media player that is playing until told to pause.
class FakePlayer final : public FakeService {
public:
    FakePlayer()
        : FakeService(QStringLiteral("speecher-fake-player"))
    {
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        if (message.member() == QLatin1String("Get")) {
            const QString status = playing ? QStringLiteral("Playing") : QStringLiteral("Paused");
            m_bus.send(message.createReply(QVariant::fromValue(QDBusVariant(status))));
            return true;
        }
        if (message.member() == QLatin1String("Pause")) {
            playing = false;
        } else if (message.member() == QLatin1String("Play")) {
            playing = true;
        }
        m_bus.send(message.createReply());
        return true;
    }

    bool playing = true;
};

class DesktopServicesTests : public QObject {
    Q_OBJECT

private slots:
    // A probe killed before it unloaded leaves its script loaded for the rest
    // of the KWin session; the next probe must still get its answer.
    void kwinProbeReplacesAScriptLeftLoaded()
    {
        FakeKWin kwin;
        kwin.leaveLoaded(QStringLiteral("speecher-active-window"));
        QVERIFY(kwin.start(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting")));

        KWinActiveWindow probe;
        const std::optional<KWinWindowInfo> window = probe.activeWindow(2000);
        QVERIFY(window);
        QCOMPARE(window->resourceClass, QStringLiteral("konsole"));
        QCOMPARE(window->internalId, QStringLiteral("{window}"));
    }

    // Media paused for one dictation and again before it resumed is still
    // resumed.
    void mediaPausedTwiceIsResumed()
    {
        FakePlayer player;
        QVERIFY(player.start(QStringLiteral("org.mpris.MediaPlayer2.speecherTest"),
                             QStringLiteral("/org/mpris/MediaPlayer2")));
        MediaPauseController media;
        media.pausePlaying();
        QTRY_VERIFY(!player.playing);
        // The player answered Pause before this, so the controller has the
        // answer by the time this one is back.
        QDBusPendingCallWatcher answered(QDBusConnection::sessionBus().asyncCall(
            QDBusMessage::createMethodCall(QStringLiteral("org.mpris.MediaPlayer2.speecherTest"),
                                           QStringLiteral("/org/mpris/MediaPlayer2"),
                                           QStringLiteral("org.mpris.MediaPlayer2.Player"),
                                           QStringLiteral("Stop"))));
        QSignalSpy finished(&answered, &QDBusPendingCallWatcher::finished);
        QTRY_VERIFY(!finished.isEmpty());
        media.pausePlaying();
        media.resumePaused();
        QTRY_VERIFY(player.playing);
    }
};

} // namespace

int runDesktopServicesTests(int argc, char **argv)
{
    DesktopServicesTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_desktop_services.moc"
