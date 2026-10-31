// The KWin active-window probe and media pausing against a fake KWin and a
// fake media player on the suite's private session bus.
#include "common/test_dbus_service.h"
#include "common/test_suites.h"

#include "platform/KWinActiveWindow.h"
#include "platform/MediaPauseController.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace speecher;

namespace {

// KWin's scripting interface. Running a loaded script calls back the probe it
// names with a fixed window. The probe blocks on its calls, so this answers
// from a thread of its own.
class FakeKWin final : public FakeDBusService {
public:
    FakeKWin()
        : FakeDBusService(QStringLiteral("speecher-fake-kwin"))
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
    // How long after each run the script reports, as a busy KWin is slow to.
    void delayReports(int ms) { m_reportDelayMs = ms; }

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
            ++m_runs;
            reportWindowTo(m_scriptPath);
        }
        return true;
    }

private:
    // Reports a different window on each run, {window-1} first, and hands
    // back the probe number the script carries.
    void reportWindowTo(const QString &scriptPath)
    {
        QFile script(scriptPath);
        if (!script.open(QIODevice::ReadOnly)) {
            return;
        }
        const QRegularExpressionMatch target =
            QRegularExpression(QStringLiteral(
                "callDBus\\('([^']*)', '([^']*)', '([^']*)', 'reportWindow'[^)]*?(?:, '(\\d+)')?\\)"))
                .match(QString::fromUtf8(script.readAll()));
        QDBusMessage report = QDBusMessage::createMethodCall(
            target.captured(1), target.captured(2), target.captured(3), QStringLiteral("reportWindow"));
        report << QStringLiteral("konsole") << QStringLiteral("konsole") << QStringLiteral("Shell")
               << QStringLiteral("4242") << QStringLiteral("{window-%1}").arg(m_runs);
        if (target.hasCaptured(4)) {
            report << target.captured(4);
        }
        QTimer::singleShot(m_reportDelayMs, this, [this, report] { m_bus.send(report); });
    }

    QThread m_thread;
    QSet<QString> m_loaded;
    QString m_scriptPath;
    int m_runs = 0;
    int m_reportDelayMs = 0;
};

// An MPRIS media player that is playing until told to pause.
class FakePlayer final : public FakeDBusService {
public:
    FakePlayer()
        : FakeDBusService(QStringLiteral("speecher-fake-player"))
    {
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        if (message.member() == QLatin1String("Get")) {
            const QString status = playing ? QStringLiteral("Playing") : QStringLiteral("Paused");
            m_bus.send(message.createReply(QVariant::fromValue(QDBusVariant(status))));
            return true;
        }
        if (message.member() == QLatin1String("Play") && refusedPlays > 0) {
            --refusedPlays;
            m_bus.send(message.createErrorReply(QDBusError::Failed, QStringLiteral("Not now")));
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
    int refusedPlays = 0;
};

// Waits until the test player has answered everything sent to it so far. The
// controller shares this connection, so it has those answers by then too.
bool playerAnsweredEarlierCalls()
{
    QDBusPendingCallWatcher answered(QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(QStringLiteral("org.mpris.MediaPlayer2.speecherTest"),
                                       QStringLiteral("/org/mpris/MediaPlayer2"),
                                       QStringLiteral("org.mpris.MediaPlayer2.Player"),
                                       QStringLiteral("Stop"))));
    QSignalSpy finished(&answered, &QDBusPendingCallWatcher::finished);
    return finished.wait(5000);
}

class DesktopServicesTests : public QObject {
    Q_OBJECT

private slots:
    // A probe killed before it unloaded leaves its script loaded for the rest
    // of the KWin session; the next probe must still get its answer.
    void kwinProbeReplacesAScriptLeftLoaded()
    {
        // The probe writes its script to the runtime directory; keep it off
        // the real one.
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
        const auto restoreRuntime = qScopeGuard([&] {
            previousRuntime.isNull() ? qunsetenv("XDG_RUNTIME_DIR")
                                     : qputenv("XDG_RUNTIME_DIR", previousRuntime);
        });
        FakeKWin kwin;
        kwin.leaveLoaded(QStringLiteral("speecher-active-window"));
        QVERIFY(kwin.start(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting")));

        KWinActiveWindow probe;
        bool answered = false;
        std::optional<KWinWindowInfo> window;
        probe.activeWindow(
            [&](const std::optional<KWinWindowInfo> &active) {
                answered = true;
                window = active;
            },
            2000);
        QTRY_VERIFY(answered);
        QVERIFY(window);
        QCOMPARE(window->resourceClass, QStringLiteral("konsole"));
        QCOMPARE(window->internalId, QStringLiteral("{window-1}"));
        QVERIFY(QFile::exists(runtime.filePath(QStringLiteral("speecher-kwin-active-window.js"))));
    }

    // The probe asks KWin without waiting in a nested event loop: it answers
    // only after activeWindow() returns, and a second ask while the first is
    // under way shares its answer.
    void kwinProbeAnswersLaterAndSharesAProbeUnderWay()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
        const auto restoreRuntime = qScopeGuard([&] {
            previousRuntime.isNull() ? qunsetenv("XDG_RUNTIME_DIR")
                                     : qputenv("XDG_RUNTIME_DIR", previousRuntime);
        });
        FakeKWin kwin;
        QVERIFY(kwin.start(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting")));

        KWinActiveWindow probe;
        QStringList answers;
        const auto record = [&answers](const std::optional<KWinWindowInfo> &window) {
            answers << (window ? window->resourceClass : QStringLiteral("none"));
        };
        probe.activeWindow(record, 2000);
        probe.activeWindow(record, 2000);
        QVERIFY(answers.isEmpty());
        QTRY_COMPARE(answers, (QStringList{QStringLiteral("konsole"), QStringLiteral("konsole")}));
    }

    // A probe that timed out can still report while the next one waits: that
    // late report is not the next probe's answer, which names the window as
    // KWin saw it for that probe.
    void kwinProbeIgnoresALateReportFromAnEarlierProbe()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
        const auto restoreRuntime = qScopeGuard([&] {
            previousRuntime.isNull() ? qunsetenv("XDG_RUNTIME_DIR")
                                     : qputenv("XDG_RUNTIME_DIR", previousRuntime);
        });
        FakeKWin kwin;
        kwin.delayReports(300);
        QVERIFY(kwin.start(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting")));

        KWinActiveWindow probe;
        std::optional<std::optional<KWinWindowInfo>> first;
        std::optional<std::optional<KWinWindowInfo>> second;
        probe.activeWindow([&first](const std::optional<KWinWindowInfo> &window) { first = window; }, 50);
        QTRY_VERIFY(first.has_value());
        QVERIFY(!*first);
        probe.activeWindow([&second](const std::optional<KWinWindowInfo> &window) { second = window; }, 2000);
        QTRY_VERIFY(second.has_value());
        QVERIFY(*second);
        QCOMPARE((*second)->internalId, QStringLiteral("{window-2}"));
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
        QVERIFY(playerAnsweredEarlierCalls());
        media.pausePlaying();
        media.resumePaused();
        QTRY_VERIFY(player.playing);
    }

    // A player that refuses Play stays paused, and the next resume asks again.
    void refusedResumeIsTriedAgain()
    {
        FakePlayer player;
        QVERIFY(player.start(QStringLiteral("org.mpris.MediaPlayer2.speecherTest"),
                             QStringLiteral("/org/mpris/MediaPlayer2")));
        MediaPauseController media;
        media.pausePlaying();
        QTRY_VERIFY(!player.playing);
        QVERIFY(playerAnsweredEarlierCalls());
        player.refusedPlays = 1;
        media.resumePaused();
        QVERIFY(playerAnsweredEarlierCalls());
        QVERIFY(!player.playing);
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
