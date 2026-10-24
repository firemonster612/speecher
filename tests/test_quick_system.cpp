#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/SpeecherRemoval.h"
#include "app/VirtualKeyboardSetup.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickSystem.h"

#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>

#include <memory>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;

namespace {

YdotoolSetupStatus status(YdotoolSetupState state, bool managed = true)
{
    YdotoolSetupStatus status;
    status.state = state;
    status.speecherManagedSetupInstalled = managed;
    return status;
}

// A computer whose virtual keyboard is whatever the test says, and which
// records what the setup asked of it. The helper steps run off the test's
// thread, so they lock.
struct FakeKeyboard {
    QMutex mutex;
    YdotoolSetupState installed = YdotoolSetupState::NotInstalled;
    bool managed = false;
    bool helperSucceeds = true;
    QString helperError;
    bool typeSucceeds = true;
    QStringList calls;

    VirtualKeyboardSetup::System system()
    {
        VirtualKeyboardSetup::System system;
        system.probe = [this](bool enabled) {
            QMutexLocker lock(&mutex);
            // Set up but off in Speecher reads as Disabled, as the real probe does.
            if (installed == YdotoolSetupState::Ready && !enabled) {
                return status(YdotoolSetupState::Disabled, managed);
            }
            return status(installed, managed);
        };
        system.runHelper = [this](YdotoolSetup::HelperAction action, QString *error) {
            QMutexLocker lock(&mutex);
            const bool install = action == YdotoolSetup::HelperAction::Install;
            calls.append(install ? QStringLiteral("install") : QStringLiteral("remove"));
            if (!helperSucceeds) {
                *error = helperError;
                return false;
            }
            installed = install ? YdotoolSetupState::Ready : YdotoolSetupState::NotInstalled;
            managed = install;
            return true;
        };
        system.startService = [this](QString *) {
            QMutexLocker lock(&mutex);
            calls.append(QStringLiteral("start"));
            return true;
        };
        system.stopService = [this](QString *) {
            QMutexLocker lock(&mutex);
            calls.append(QStringLiteral("stop"));
            return true;
        };
        system.type = [this](const QString &text, QString *) {
            QMutexLocker lock(&mutex);
            calls.append(QStringLiteral("type ") + text);
            return typeSucceeds;
        };
        return system;
    }
};

SpeecherRemoval::Steps quietRemoval()
{
    SpeecherRemoval::Steps steps;
    steps.removeDesktopIntegration = [] { return DesktopIntegrationRemoval{}; };
    steps.removeShortcutRegistration = [](QString *) { return true; };
    steps.virtualKeyboardInstalled = [] { return false; };
    steps.stopVirtualKeyboard = [](QString *) { return true; };
    steps.removeVirtualKeyboard = [](QString *) { return true; };
    steps.keyHelperInstalled = [] { return false; };
    steps.removeKeyHelper = [](QString *) { return true; };
    steps.deleteSecrets = [](QString *) { return true; };
    steps.deleteSettings = [](QString *) { return true; };
    return steps;
}

QVariantMap keyboardCheck(const QuickSystem &system)
{
    return system.check(QStringLiteral("virtualKeyboardHealth"));
}

} // namespace

// The UI's System singleton over core's virtual keyboard setup, phone
// transfer and removal, with stand-ins for the computer.
class QuickSystemTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    // The keyboard is set up through the helper, then waits for the typing
    // test, and turns on only when the field gets exactly the test text.
    void theVirtualKeyboardTurnsOnOnlyAfterTheTypingTestPasses()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("notInstalled"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("fix")).toString(), QStringLiteral("setUp"));

        system.fix(QStringLiteral("virtualKeyboardHealth"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("state")).toString(), QStringLiteral("working"));
        QTRY_COMPARE(system.virtualKeyboardTest(), QStringLiteral("ready"));
        QCOMPARE(keyboard.calls, (QStringList{QStringLiteral("install"), QStringLiteral("start")}));
        QVERIFY(!SettingsStore().ydotoolEnabled());

        // What arrives before a run, or other text, turns nothing on.
        system.virtualKeyboardTestReceived(VirtualKeyboardSetup::testText());
        system.runVirtualKeyboardTest();
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("typing"));
        QTRY_VERIFY(keyboard.calls.contains(QStringLiteral("type ") + VirtualKeyboardSetup::testText()));
        system.virtualKeyboardTestReceived(QStringLiteral("speecher"));
        QVERIFY(!SettingsStore().ydotoolEnabled());

        system.virtualKeyboardTestReceived(VirtualKeyboardSetup::testText());
        QVERIFY(SettingsStore().ydotoolEnabled());
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("off"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QCOMPARE(system.virtualKeyboardActions(), (QStringList{QStringLiteral("disable"), QStringLiteral("remove")}));

        system.disableVirtualKeyboard();
        QVERIFY(!SettingsStore().ydotoolEnabled());
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("disabled"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("fix")).toString(), QStringLiteral("enable"));
    }

    void aTestWhoseWordsNeverArriveMisses()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("disabled"));

        // Already set up: turning it on goes straight to the test.
        system.fix(QStringLiteral("virtualKeyboardHealth"));
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("ready"));
        QVERIFY(keyboard.calls.isEmpty());
        system.runVirtualKeyboardTest();
        QTRY_COMPARE_WITH_TIMEOUT(system.virtualKeyboardTest(), QStringLiteral("missed"),
                                  VirtualKeyboardSetup::testArrivalMs * 3);
        QVERIFY(!SettingsStore().ydotoolEnabled());

        // A keyboard that can't type says so in core's words, and stays off.
        keyboard.typeSucceeds = false;
        QSignalSpy announced(&system, &QuickSystem::announced);
        system.runVirtualKeyboardTest();
        QTRY_COMPARE(announced.count(), 1);
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("ready"));
        QVERIFY(!SettingsStore().ydotoolEnabled());
    }

    // Declining the password prompt leaves everything as it was, and says why.
    void aDeclinedSetupChangesNothing()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("Authorization was declined.");
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QSignalSpy announced(&system, &QuickSystem::announced);

        system.fix(QStringLiteral("virtualKeyboardHealth"));
        QTRY_COMPARE(announced.count(), 1);
        QCOMPARE(announced.first().first().toString(), QStringLiteral("Authorization was declined."));
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("off"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("notInstalled"));
        QCOMPARE(keyboard.calls, QStringList{QStringLiteral("install")});
    }

    void removingTheKeyboardStopsItAndSaysWhatStayed()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        SettingsStore().setYdotoolEnabled(true);
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QSignalSpy announced(&system, &QuickSystem::announced);

        system.removeVirtualKeyboard();
        QVERIFY(system.virtualKeyboardActions().isEmpty());
        QTRY_COMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("notInstalled"));
        QCOMPARE(keyboard.calls, (QStringList{QStringLiteral("stop"), QStringLiteral("remove")}));
        QVERIFY(!SettingsStore().ydotoolEnabled());
        QCOMPARE(announced.count(), 0);

        // A helper that fails keeps the setting and names the failure.
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("pkexec failed");
        SettingsStore().setYdotoolEnabled(true);
        system.removeVirtualKeyboard();
        QTRY_COMPARE(announced.count(), 1);
        QCOMPARE(announced.first().first().toString(), QStringLiteral("pkexec failed"));
        QVERIFY(SettingsStore().ydotoolEnabled());
    }

    // Removal goes on past a failed step and reports each one.
    void removalReportsEveryStep()
    {
        SpeecherRemoval::Steps steps = quietRemoval();
        QStringList ran;
        steps.removeDesktopIntegration = [] {
            DesktopIntegrationRemoval files;
            files.removed = {QStringLiteral("app menu entry")};
            files.absent = {QStringLiteral("icon")};
            files.failed = {QStringLiteral("speecher command: the link does not point to a Speecher AppImage")};
            return files;
        };
        steps.removeShortcutRegistration = [](QString *error) {
            *error = QStringLiteral("The portal did not answer.");
            return false;
        };
        steps.virtualKeyboardInstalled = [] { return true; };
        steps.removeVirtualKeyboard = [](QString *error) {
            *error = QStringLiteral("Authorization was declined.");
            return false;
        };
        steps.deleteSettings = [&ran](QString *) {
            ran.append(QStringLiteral("settings"));
            return true;
        };
        steps.programFile = QStringLiteral("/home/fox/Applications/Speecher.AppImage");
        SpeecherRemoval removal(steps);
        QSignalSpy finished(&removal, &SpeecherRemoval::finished);

        removal.start(false);
        QVERIFY(removal.running());
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!removal.running());
        const auto report = finished.first().first().value<RemovalReport>();
        QCOMPARE(report.removed, QStringList{QStringLiteral("app menu entry")});
        QCOMPARE(report.problems,
                 (QStringList{QStringLiteral("Could not remove the speecher command: the link does not point to a Speecher AppImage."),
                              QStringLiteral("The portal did not answer."), QStringLiteral("Authorization was declined.")}));
        QVERIFY(!report.settingsDeleted);
        QVERIFY(ran.isEmpty());
        QCOMPARE(report.programFile, QStringLiteral("/home/fox/Applications/Speecher.AppImage"));

        removal.start(true);
        QTRY_COMPARE(finished.count(), 2);
        const auto deleting = finished.last().first().value<RemovalReport>();
        QVERIFY(deleting.settingsDeleted);
        QVERIFY(deleting.removed.contains(QStringLiteral("settings")));
        QCOMPARE(ran, QStringList{QStringLiteral("settings")});
    }

    void uninstallShowsItsReport()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());

        QVERIFY(system.removal().isEmpty());
        system.uninstall(true);
        QVERIFY(system.removing());
        QTRY_VERIFY(!system.removing());
        QCOMPARE(system.removal().value(QStringLiteral("removed")).toStringList(),
                 QStringList{QStringLiteral("Global Shortcut registration")});
        QVERIFY(system.removal().value(QStringLiteral("problems")).toStringList().isEmpty());
        QVERIFY(!system.removal().value(QStringLiteral("settingsDeleted")).toBool());
    }

    // The code exists only between start and stop, and a new one replaces it.
    void thePhoneCodeLivesWhileItShows()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QCOMPARE(system.phoneTransfer(), QString());

        system.startPhoneTransfer();
        const QString first = system.phoneLink();
        if (system.phoneTransfer() == QLatin1String("noNetwork")) {
            QVERIFY(first.isEmpty());
        } else {
            QCOMPARE(system.phoneTransfer(), QStringLiteral("waiting"));
            QVERIFY(first.startsWith(QStringLiteral("speecher://import?")));
            system.startPhoneTransfer();
            QVERIFY(system.phoneLink() != first);
        }
        system.stopPhoneTransfer();
        QCOMPARE(system.phoneTransfer(), QString());
        QCOMPARE(system.phoneLink(), QString());
        QCOMPARE(system.phoneBundle().value(QStringLiteral("terms")).toInt(), 0);
    }

    // The Pasting page's app type rows need accessibility to know the app.
    void pastingTypeRowsFollowAccessibility()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/PastingPage.qml")));
        const std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));

        int typeRows = 0;
        QList<QQuickItem *> items{qobject_cast<QQuickItem *>(page.get())};
        while (!items.isEmpty()) {
            QQuickItem *item = items.takeLast();
            items += item->childItems();
            const QString rowId = item->property("rowId").toString();
            if (rowId.startsWith(QLatin1String("categoryPasteRule_"))) {
                ++typeRows;
                QCOMPARE(item->property("enabled").toBool(), system.accessibility());
            }
        }
        QCOMPARE(typeRows, 7);
    }
};

int runQuickSystemTests(int argc, char **argv)
{
    QuickSystemTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_system.moc"
