#include "common/test_suites.h"

#include "core/ShortcutBinding.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/KeywatchSetup.h"
#include "platform/RoutingShortcutBinder.h"
#include "setup/KeywatchProtocol.h"

#include <QTest>

#include <memory>

using namespace speecher;

namespace {

// A binder a router can drive without a desktop: it records what it was asked
// and answers a fixed refusal for the bindings it does not take.
class FakeBinder final : public GlobalShortcutBinder {
public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    bool supported() const override { return supportedValue; }
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

    bool supportedValue = true;
    bool takesSingleKey = false;
    QString refusal = QStringLiteral("no");
    int bindCount = 0;
    int removeCount = 0;

private:
    ShortcutBinding m_shortcut;
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

        // The owner-only socket needs no group or sign-out: once it exists and
        // is writable for the login user, the helper is ready straight away.
        KeywatchProbeFacts ready = listening;
        ready.socketWritable = true;
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
};

#include "test_keywatch.moc"

int runKeywatchTests(int argc, char **argv)
{
    KeywatchTests tests;
    return runTestSuite(&tests, argc, argv);
}
