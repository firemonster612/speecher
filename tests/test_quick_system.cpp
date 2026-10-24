#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/SpeecherRemoval.h"
#include "app/VirtualKeyboardSetup.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickDictation.h"
#include "frontend/quick/host/QuickInsights.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickStatus.h"
#include "frontend/quick/host/QuickSystem.h"
#include "frontend/quick/host/QuickUpdates.h"

#include <QFile>
#include <QJSValue>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

#include <memory>
#include <tuple>

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
    for (const QVariant &check : system.checks()) {
        if (check.toMap().value(QStringLiteral("id")) == QStringLiteral("virtualKeyboardHealth")) {
            return check.toMap();
        }
    }
    return {};
}

QVariantList keyboardFailures(const QuickSystem &system)
{
    return keyboardCheck(system).value(QStringLiteral("failures")).toList();
}

QVariantMap failure(const char *step, const QString &error)
{
    return {{QStringLiteral("step"), QLatin1String(step)}, {QStringLiteral("error"), error}};
}

// A QML var property as plain values.
QVariant plain(const QVariant &value)
{
    return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

std::unique_ptr<QObject> createQml(QQmlEngine &engine, const QByteArray &qml, const char *url = "")
{
    QQmlComponent component(&engine);
    component.setData(qml, QUrl(QLatin1String(url)));
    std::unique_ptr<QObject> created(component.create());
    if (!created) {
        qWarning().noquote() << component.errorString();
    }
    return created;
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

        // A keyboard that can't type says so, and stays off.
        keyboard.typeSucceeds = false;
        system.runVirtualKeyboardTest();
        QTRY_COMPARE(keyboardFailures(system), QVariantList{failure("type", QString())});
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

        system.fix(QStringLiteral("virtualKeyboardHealth"));
        QTRY_COMPARE(keyboardFailures(system),
                     QVariantList{failure("setUp", QStringLiteral("Authorization was declined."))});
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

        system.removeVirtualKeyboard();
        QVERIFY(system.virtualKeyboardActions().isEmpty());
        QTRY_COMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("notInstalled"));
        QCOMPARE(keyboard.calls, (QStringList{QStringLiteral("stop"), QStringLiteral("remove")}));
        QVERIFY(!SettingsStore().ydotoolEnabled());
        QVERIFY(keyboardFailures(system).isEmpty());

        // A helper that fails keeps the setting and names the failure.
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("pkexec failed");
        SettingsStore().setYdotoolEnabled(true);
        system.removeVirtualKeyboard();
        QTRY_COMPARE(keyboardFailures(system), QVariantList{failure("remove", QStringLiteral("pkexec failed"))});
        QVERIFY(SettingsStore().ydotoolEnabled());
    }

    // Only the setup assistant's own setup makes the output method Automatic,
    // as the assistant did before.
    void setupsKeyboardPastesAutomatically()
    {
        ApplicationController controller(true);
        const QString ydotool = QString::fromLatin1(OutputMethod::Ydotool);
        // The output method after setting up a keyboard that isn't there yet.
        const auto afterSetUp = [&controller, &ydotool](bool fromSetup, bool helperSucceeds) {
            controller.settings()->setOutputMethod(ydotool);
            FakeKeyboard keyboard;
            keyboard.helperSucceeds = helperSucceeds;
            QuickSystem system(&controller, keyboard.system(), quietRemoval());
            const QString id = QStringLiteral("virtualKeyboardHealth");
            fromSetup ? system.fixFromSetup(id) : system.fix(id);
            const bool finished = QTest::qWaitFor([&system] {
                return keyboardCheck(system).value(QStringLiteral("state")).toString() != QLatin1String("working");
            });
            return finished ? SettingsStore().outputMethod() : QStringLiteral("still setting up");
        };
        QCOMPARE(afterSetUp(false, true), ydotool);
        QCOMPARE(afterSetUp(true, true), QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(afterSetUp(true, false), ydotool);
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
        steps.keyHelperInstalled = [] { return true; };
        steps.removeKeyHelper = [](QString *error) {
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
        using Part = RemovalStep::Part;
        using Outcome = RemovalStep::Outcome;
        const auto outcomes = [](const RemovalReport &report) {
            QList<std::tuple<Part, Outcome, QString>> outcomes;
            for (const RemovalStep &step : report.steps) {
                outcomes.append({step.part, step.outcome, step.error});
            }
            return outcomes;
        };
        const auto report = finished.first().first().value<RemovalReport>();
        QCOMPARE(report.files.removed, QStringList{QStringLiteral("app menu entry")});
        QCOMPARE(report.files.absent, QStringList{QStringLiteral("icon")});
        QCOMPARE(report.files.failed,
                 QStringList{QStringLiteral("speecher command: the link does not point to a Speecher AppImage")});
        // Each failure is labelled by its part, so two declined prompts read apart.
        QCOMPARE(outcomes(report),
                 (QList<std::tuple<Part, Outcome, QString>>{
                     {Part::ShortcutRegistration, Outcome::Failed, QStringLiteral("The portal did not answer.")},
                     {Part::VirtualKeyboard, Outcome::Failed, QStringLiteral("Authorization was declined.")},
                     {Part::KeyHelper, Outcome::Failed, QStringLiteral("Authorization was declined.")},
                     {Part::Settings, Outcome::Kept, QString()},
                 }));
        QVERIFY(!report.settingsDeleted());
        QVERIFY(ran.isEmpty());
        QCOMPARE(report.programFile, QStringLiteral("/home/fox/Applications/Speecher.AppImage"));

        removal.start(true);
        QTRY_COMPARE(finished.count(), 2);
        const auto deleting = finished.last().first().value<RemovalReport>();
        QVERIFY(deleting.settingsDeleted());
        QCOMPARE(std::get<Part>(outcomes(deleting).last()), Part::Settings);
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
        const auto step = [](const char *part, const char *outcome) {
            return QVariantMap{{QStringLiteral("part"), QLatin1String(part)},
                               {QStringLiteral("outcome"), QLatin1String(outcome)},
                               {QStringLiteral("error"), QString()}};
        };
        QCOMPARE(system.removal().value(QStringLiteral("steps")).toList(),
                 (QVariantList{step("shortcutRegistration", "removed"), step("settings", "kept")}));
        QCOMPARE(system.removal().value(QStringLiteral("files")).toMap().value(QStringLiteral("removed")).toStringList(),
                 QStringList{});
        QVERIFY(!system.removal().value(QStringLiteral("settingsDeleted")).toBool());
    }

    // Once removal has deleted the settings, an edit made before quitting
    // doesn't bring the file back, whether it goes through a snapshot or a
    // setter.
    void deletedSettingsStayDeleted()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        QuickSystem system(&controller, keyboard.system(), steps);
        settings.set(QStringLiteral("themeControl"), QStringLiteral("dark"));
        const QString file = controller.settings()->raw().fileName();
        controller.settings()->raw().sync();
        QVERIFY(QFile::exists(file));

        system.uninstall(false);
        QTRY_VERIFY(!system.removing());
        QVERIFY(system.removal().value(QStringLiteral("settingsDeleted")).toBool());
        QVERIFY(!QFile::exists(file));

        settings.set(QStringLiteral("themeControl"), QStringLiteral("light"));
        controller.settings()->setYdotoolEnabled(true);
        QCoreApplication::processEvents();
        controller.settings()->raw().sync();
        QVERIFY(!QFile::exists(file));
        QVERIFY(controller.settings()->ydotoolEnabled());
        // A reader made afterwards, as a provider check makes one.
        { SettingsStore reader; Q_UNUSED(reader.ydotoolEnabled()); }
        QVERIFY(!QFile::exists(file));
    }

    // A keyboard setup under way finishes, saving what it saves, before
    // removal starts and deletes the settings.
    void removalWaitsForTheKeyboardSetup()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        QString methodAtStart;
        steps.removeDesktopIntegration = [&controller, &methodAtStart] {
            methodAtStart = controller.settings()->outputMethod();
            return DesktopIntegrationRemoval{};
        };
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        QuickSystem system(&controller, keyboard.system(), steps);
        controller.settings()->setOutputMethod(QStringLiteral("clipboard"));
        const QString file = controller.settings()->raw().fileName();

        system.fixFromSetup(QStringLiteral("virtualKeyboardHealth"));
        system.uninstall(false);
        QVERIFY(system.removing());
        QTRY_VERIFY(!system.removing());
        QCOMPARE(methodAtStart, QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("off"));
        QCoreApplication::processEvents();
        QVERIFY(!QFile::exists(file));
    }

    // Closing Main while removal runs quits once removal has deleted the
    // settings, and from then on Main holds only the report.
    void closingMainBeforeRemovalFinishesQuits()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickStatus status(&controller);
        QuickDictation dictation(&controller);
        QuickUpdates updates(&controller, controller.updates());
        QuickModels models(&controller);
        QuickInsights insights(&controller);
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        QuickSystem system(&controller, keyboard.system(), steps);
        QSignalSpy quit(&controller, &ApplicationController::quitRequested);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/Main.qml")));
        const std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window));

        system.uninstall(false);
        window->hide();
        QVERIFY(system.removing());
        QCOMPARE(quit.count(), 0);
        QTRY_COMPARE(quit.count(), 1);
        auto *report = window->findChild<QQuickItem *>(QStringLiteral("removalReport"));
        QVERIFY(report);
        QVERIFY(report->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("sidebar"))->isVisible());
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

    // A page that hides takes its code with it; showing it again starts
    // without one, and the next code is new.
    void hidingMainStopsThePhoneCode()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        // General clears Insights' history, as the host always has one.
        QuickInsights insights(&controller);
        QQmlEngine engine;
        const std::unique_ptr<QObject> created = createQml(engine, R"(
            import QtQuick
            Window { width: 1000; height: 900; visible: true; GeneralPage { objectName: "page"; anchors.fill: parent } }
        )", "qrc:/qt/qml/Speecher/App/pages/Test.qml");
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QObject *page = window->findChild<QObject *>(QStringLiteral("page"));
        QVERIFY(page);

        QMetaObject::invokeMethod(page, "showPhoneCode", Q_ARG(QVariant, true));
        QVERIFY(!system.phoneTransfer().isEmpty());
        const QString first = system.phoneLink();
        window->hide();
        QCOMPARE(system.phoneTransfer(), QString());
        QVERIFY(!page->property("_phoneOpen").toBool());

        window->show();
        QCOMPARE(system.phoneTransfer(), QString());
        QMetaObject::invokeMethod(page, "showPhoneCode", Q_ARG(QVariant, true));
        QVERIFY(!system.phoneTransfer().isEmpty());
        if (!first.isEmpty()) {
            QVERIFY(system.phoneLink() != first);
        }
    }

    // A row follows its check as it changes, and a fix that fails says why
    // in the row, as the setup assistant's row does.
    void aHealthRowFollowsItsCheck()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("Authorization was declined.");
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        QQmlEngine engine;
        const std::unique_ptr<QObject> row = createQml(engine, R"(
            import Speecher.App
            HealthRow { rowId: "virtualKeyboardHealth"; fromSetup: true }
        )");
        QVERIFY(row);
        const auto reason = [&row] { return row->property("check").toMap().value(QStringLiteral("reason")).toString(); };
        QCOMPARE(reason(), QStringLiteral("notInstalled"));

        system.fixFromSetup(QStringLiteral("virtualKeyboardHealth"));
        QCOMPARE(reason(), QStringLiteral("settingUp"));
        QTRY_COMPARE(row->property("help").toString(),
                     QStringLiteral("Setup failed: Authorization was declined."));
        QCOMPARE(reason(), QStringLiteral("notInstalled"));

        // Trying again clears it.
        keyboard.helperSucceeds = true;
        system.fixFromSetup(QStringLiteral("virtualKeyboardHealth"));
        QVERIFY(!row->property("help").toString().startsWith(QLatin1String("Setup failed")));
        QTRY_COMPARE(reason(), QStringLiteral("testing"));
    }

    // Setup goes on past a check that is as far as this computer gets, and
    // the check still says so.
    void setupSettlesForWhatTheComputerAllows()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::NeedsSignOut;
        keyboard.managed = true;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        // Setup's choices read Models, as the host always has one.
        QuickModels models(&controller);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(component.create());
        QVERIFY2(choices, qPrintable(component.errorString()));
        QVariantMap delivery;
        for (const QVariant &step : plain(choices->property("steps")).toList()) {
            if (step.toMap().value(QStringLiteral("id")) == QLatin1String("delivery")) {
                delivery = step.toMap();
            }
        }
        QVERIFY(delivery.value(QStringLiteral("done")).toBool());
        QCOMPARE(delivery.value(QStringLiteral("verdict")).toString(), QStringLiteral("Sign out first"));

        const std::unique_ptr<QObject> settled = createQml(engine, R"(
            import QtQuick
            import Speecher.App
            QtObject {
                property var results: [
                    { id: "desktopAccessibility", state: "off", reason: "unsupported" },
                    { id: "desktopAccessibility", state: "attention", reason: "sessionOnly" },
                    { id: "desktopAccessibility", state: "off", reason: "off" },
                    { id: "virtualKeyboardHealth", state: "attention", reason: "daemonNotRunning" },
                    { id: "globalShortcuts", state: "off", reason: "unsupported" }
                ].map(check => Setup.settled(check))
            }
        )");
        QVERIFY(settled);
        QCOMPARE(plain(settled->property("results")).toList(), (QVariantList{true, true, false, false, false}));
    }

    // The Pasting page's app type rows need accessibility to know the app.
    void pastingTypeRowsFollowAccessibility()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        QuickSystem system(&controller, keyboard.system(), quietRemoval());
        // The page's Library reads Models, as the host always has one.
        QuickModels models(&controller);
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
