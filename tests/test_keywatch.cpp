#include "common/test_suites.h"

#include "core/SettingsStore.h"
#include "core/ShortcutBinding.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/KeywatchSetup.h"
#include "platform/KeywatchShortcutBinder.h"
#include "platform/RoutingShortcutBinder.h"
#include "setup/KeywatchProtocol.h"

#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <mutex>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace speecher;

namespace {

// A binder a router can drive without a desktop: it records what it was asked
// and answers a fixed refusal for the bindings it does not take.
class FakeBinder final : public GlobalShortcutBinder {
public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    bool supported() const override { return supportedValue; }
    bool usesDesktopShortcutChooser() const override { return desktopChooser; }
    QString unsupportedReason() const override { return {}; }
    void bind() override { bindCount += 1; }
    ShortcutBinding shortcut() const override { return m_shortcut; }
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override
    {
        return takesSingleKey == binding.isSingleKey() ? QString() : refusal;
    }
    bool setShortcut(const ShortcutBinding &shortcut, QString *error) override
    {
        const QString reason = unsupportedBindingReason(shortcut);
        if (!reason.isEmpty()) {
            if (error) {
                *error = reason;
            }
            return false;
        }
        m_shortcut = shortcut;
        emit bindingChanged();
        return true;
    }
    bool removeRegistration(QString *) override
    {
        removeCount += 1;
        return true;
    }

    void fireActivated() { emit activated(); }
    void fireDeactivated() { emit deactivated(); }

    bool supportedValue = true;
    bool desktopChooser = false;
    bool takesSingleKey = false;
    QString refusal = QStringLiteral("no");
    int bindCount = 0;
    int removeCount = 0;

private:
    ShortcutBinding m_shortcut;
};

// Stands in for speecher-keywatchd. It answers on a thread of its own because
// the binder waits for the reply synchronously: each connection gets the next
// refusal in line, and the key it asked for is recorded so the test can send
// that connection key events.
class FakeKeywatchDaemon {
public:
    struct Watch {
        quint8 keyId;
        int fd;
    };

    FakeKeywatchDaemon(const QString &path, QList<keywatch::Refusal> replies)
        : m_listener(socket(AF_UNIX, SOCK_STREAM, 0))
    {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        qstrncpy(address.sun_path, QFile::encodeName(path).constData(), sizeof(address.sun_path));
        bind(m_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        listen(m_listener, 4);
        m_thread = std::thread([this, replies] {
            for (const keywatch::Refusal refusal : replies) {
                const int client = accept(m_listener, nullptr, nullptr);
                if (client < 0) {
                    return;
                }
                keywatch::WatchRequest request{};
                if (read(client, &request, sizeof(request)) != sizeof(request)) {
                    close(client);
                    return;
                }
                const keywatch::WatchReply reply{request.version, quint8(refusal)};
                (void)write(client, &reply, sizeof(reply));
                const std::lock_guard lock(m_mutex);
                m_watches.append({request.keyId, client});
            }
        });
    }

    ~FakeKeywatchDaemon()
    {
        shutdown(m_listener, SHUT_RDWR);
        m_thread.join();
        close(m_listener);
        for (const Watch &watch : watches()) {
            close(watch.fd);
        }
    }

    QList<Watch> watches() const
    {
        const std::lock_guard lock(m_mutex);
        return m_watches;
    }

    static void send(const Watch &watch, bool down, quint64 monotonicUsec)
    {
        keywatch::KeyEvent event{};
        event.down = down ? 1 : 0;
        event.monotonicUsec = monotonicUsec;
        QCOMPARE(write(watch.fd, &event, sizeof(event)), qint64(sizeof(event)));
    }

private:
    int m_listener;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    QList<Watch> m_watches;
};

// The real binder over the fake daemon. The probe looks for an installed
// helper at its system path, so the per-binding check skips it here.
class DaemonlessKeywatchBinder final : public KeywatchShortcutBinder {
public:
    using KeywatchShortcutBinder::KeywatchShortcutBinder;

    QString unsupportedBindingReason(const ShortcutBinding &binding) const override
    {
        return SingleKeyShortcutBinder::unsupportedBindingReason(binding);
    }
};

} // namespace

class KeywatchTests : public QObject {
    Q_OBJECT

private slots:
    // The pure evaluate() maps facts to a state; this is what gets unit tested,
    // never the probe.
    void evaluateMapsFactsToStates()
    {
        QCOMPARE(KeywatchSetup::evaluate({}).state, KeywatchSetupState::NotInstalled);

        KeywatchProbeFacts installed;
        installed.socketUnitInstalled = true;
        QCOMPARE(KeywatchSetup::evaluate(installed).state,
                 KeywatchSetupState::DaemonNotRunning);

        // The socket is up but not yet reachable: still not ready.
        KeywatchProbeFacts listening = installed;
        listening.socketExists = true;
        QCOMPARE(KeywatchSetup::evaluate(listening).state,
                 KeywatchSetupState::DaemonNotRunning);

        // A writable socket is systemd's, not the daemon's: a daemon that
        // cannot read keyboards leaves exactly this state behind, so it is not
        // ready.
        KeywatchProbeFacts writable = listening;
        writable.socketWritable = true;
        const KeywatchSetupStatus silent = KeywatchSetup::evaluate(writable);
        QCOMPARE(silent.state, KeywatchSetupState::DaemonNotRunning);
        QVERIFY(!silent.ready());

        // A daemon that answers with another protocol version is alive but
        // cannot serve this build.
        KeywatchProbeFacts mismatched = writable;
        mismatched.daemonAnswers = true;
        const KeywatchSetupStatus stale = KeywatchSetup::evaluate(mismatched);
        QCOMPARE(stale.state, KeywatchSetupState::NeedsReinstall);
        QVERIFY(!stale.ready());

        // A version 1 daemon still watches one key, so a single-key dictation
        // shortcut keeps working.
        KeywatchProbeFacts older = mismatched;
        older.daemonOutdated = true;
        const KeywatchSetupStatus outdated = KeywatchSetup::evaluate(older);
        QCOMPARE(outdated.state, KeywatchSetupState::Outdated);
        QVERIFY(outdated.ready());

        // The owner-only socket needs no group or sign-out: once the daemon
        // answers on it with this build's protocol, the helper is ready
        // straight away.
        KeywatchProbeFacts ready = mismatched;
        ready.daemonProtocolMatches = true;
        const KeywatchSetupStatus status = KeywatchSetup::evaluate(ready);
        QCOMPARE(status.state, KeywatchSetupState::Ready);
        QVERIFY(status.ready());
    }

    // The allowlist is what stops twenty-six one-key watches becoming a
    // keylogger: modifiers and F13-F24 are named, letters are not.
    void allowlistPermitsOnlyKeysThatCannotSpell()
    {
        QVERIFY(keywatch::permittedKeyByCode("AltRight"));
        QVERIFY(keywatch::permittedKeyByCode("CapsLock"));
        QVERIFY(keywatch::permittedKeyByCode("F24"));
        QVERIFY(!keywatch::permittedKeyByCode("KeyE"));
        QVERIFY(!keywatch::permittedKeyByCode("Space"));
        QVERIFY(!keywatch::permittedKeyByCode("F1"));

        // The id round-trips and the daemon holds the evdev code itself.
        const auto *altRight = keywatch::permittedKeyByCode("AltRight");
        QCOMPARE(keywatch::permittedKeyById(altRight->id)->evdev, quint16(100));
    }

    // The helper refusing a replacement key leaves the shortcut on the old
    // key, so the old key has to stay watched, not just look bound. Events
    // carry the daemon's timestamps, so a release this binder reads late
    // still reports the physical hold.
    void keywatchKeepsTheOldKeyWhenTheHelperRefusesAReplacement()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("keywatch"));
        FakeKeywatchDaemon daemon(path, {keywatch::Refusal::None, keywatch::Refusal::TooManyRequests,
                                         keywatch::Refusal::None});
        SettingsStore settings;
        const auto forget = qScopeGuard([&] { settings.raw().remove(QStringLiteral("shortcuts/toggleDictation")); });
        DaemonlessKeywatchBinder binder(&settings, GlobalShortcutBinder::actionFor(GlobalShortcutRole::Dictation),
                                        nullptr, path);
        QSignalSpy pressed(&binder, &GlobalShortcutBinder::activated);
        QSignalSpy released(&binder, &GlobalShortcutBinder::deactivated);

        const ShortcutBinding rightAlt = ShortcutBinding::singleKey(QStringLiteral("AltRight"));
        QVERIFY(binder.setShortcut(rightAlt));
        QString error;
        QVERIFY(!binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("ShiftRight")), &error));
        QCOMPARE(error, QStringLiteral("The key helper refused the watch: too many requests; try again in a minute."));
        QCOMPARE(binder.shortcut(), rightAlt);

        QTRY_COMPARE(daemon.watches().size(), 3);
        const FakeKeywatchDaemon::Watch restored = daemon.watches().at(2);
        QCOMPARE(restored.keyId, keywatch::permittedKeyByCode("AltRight")->id);

        FakeKeywatchDaemon::send(restored, true, 1'000'000);
        FakeKeywatchDaemon::send(restored, false, 1'080'000);
        QTRY_COMPARE(released.count(), 1);
        QCOMPARE(pressed.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), 80);
    }

    // supported() reads the helper's last reply, which lands after the probe
    // that asked for it, so a reply has to tell the settings page to look again.
    void keywatchSupportChangesWhenTheHelperAnswers()
    {
        SettingsStore settings;
        KeywatchShortcutBinder binder(&settings);
        QSignalSpy supportChanged(&binder, &GlobalShortcutBinder::supportChanged);
        emit KeywatchSetup::daemonAnswer()->changed();
        QCOMPARE(supportChanged.count(), 1);
    }

    // The router sends a combination to the desktop service and a single key to
    // the key watcher; setting either kind lets go of the other, since there is
    // one binding.
    void routerSendsEachBindingToItsBackend()
    {
        auto *combination = new FakeBinder;
        auto *singleKey = new FakeBinder;
        singleKey->takesSingleKey = true;
        RoutingShortcutBinder router(combination, singleKey);

        const ShortcutBinding combo(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        QVERIFY(router.setShortcut(combo));
        QCOMPARE(router.shortcut(), combo);
        QCOMPARE(combination->shortcut(), combo);

        const ShortcutBinding rightAlt = ShortcutBinding::singleKey(QStringLiteral("AltRight"));
        QVERIFY(router.setShortcut(rightAlt));
        QCOMPARE(router.shortcut(), rightAlt);
        // Setting the single key cleared the desktop registration…
        QCOMPARE(combination->removeCount, 1);
        // …and the combination binder no longer owns the binding.
        QVERIFY(!combination->shortcut().isSingleKey());
        QCOMPARE(router.unsupportedBindingReason(rightAlt), QString());
    }

    // X11 outside Plasma: the portal cannot take a combination there, but the
    // key watcher can take a single key, so the shortcut controls stay and
    // record the key instead of handing over to the desktop's chooser.
    void routerIsSupportedWhenOnlyTheSingleKeyBackendIs()
    {
        auto *combination = new FakeBinder;
        combination->supportedValue = false;
        combination->desktopChooser = true;
        auto *singleKey = new FakeBinder;
        singleKey->takesSingleKey = true;
        RoutingShortcutBinder router(combination, singleKey);

        QVERIFY(router.supported());
        QVERIFY(router.supportKnown());
        QVERIFY(!router.usesDesktopShortcutChooser());

        singleKey->supportedValue = false;
        QVERIFY(!router.supported());
        QVERIFY(router.usesDesktopShortcutChooser());
    }

    // The one-binding invariant across startup and recovery: while a single
    // key is stored, bind() never registers the combination, and a single-key
    // backend binding on its own later (the mac grant poll, the keywatch
    // helper appearing) lets a bound combination go.
    void routerKeepsOneBindingThroughBindAndRecovery()
    {
        auto *combination = new FakeBinder;
        auto *singleKey = new FakeBinder;
        singleKey->takesSingleKey = true;
        RoutingShortcutBinder router(combination, singleKey);

        QVERIFY(singleKey->setShortcut(ShortcutBinding::singleKey(QStringLiteral("AltRight")),
                                       nullptr));
        const int removedBySet = combination->removeCount;
        router.bind();
        QCOMPARE(singleKey->bindCount, 1);
        QCOMPARE(combination->bindCount, 0);

        // Recovery: the single-key backend re-binds without going through the
        // router, as the grant poll does; the combination must be let go.
        QVERIFY(singleKey->setShortcut(ShortcutBinding::singleKey(QStringLiteral("F13")),
                                       nullptr));
        QCOMPARE(combination->removeCount, removedBySet + 1);
    }

    // A backend whose desktop registration lingers after the binding moved to
    // the other backend must not start dictation; a release still passes from
    // either so a latched activation can always clear.
    void routerForwardsActivationOnlyFromTheOwningBackend()
    {
        auto *combination = new FakeBinder;
        auto *singleKey = new FakeBinder;
        singleKey->takesSingleKey = true;
        RoutingShortcutBinder router(combination, singleKey);
        QSignalSpy activations(&router, &GlobalShortcutBinder::activated);
        QSignalSpy releases(&router, &GlobalShortcutBinder::deactivated);

        // No single key is set, so the combination owns the binding.
        combination->fireActivated();
        singleKey->fireActivated();
        QCOMPARE(activations.count(), 1);

        QVERIFY(router.setShortcut(ShortcutBinding::singleKey(QStringLiteral("AltRight"))));
        combination->fireActivated();
        singleKey->fireActivated();
        QCOMPARE(activations.count(), 2);

        combination->fireDeactivated();
        singleKey->fireDeactivated();
        QCOMPARE(releases.count(), 2);
    }
};

#include "test_keywatch.moc"

int runKeywatchTests(int argc, char **argv)
{
    KeywatchTests tests;
    return runTestSuite(&tests, argc, argv);
}
