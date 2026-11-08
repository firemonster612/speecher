#include "common/test_settings_paths.h"
#include "common/test_suites.h"
#include "common/test_system_singletons.h"

#include "app/AccessibilityMonitor.h"
#include "core/PasteKeys.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickAppearance.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickDictation.h"
#include "frontend/quick/host/QuickApps.h"
#include "frontend/quick/host/QuickHistory.h"
#include "frontend/quick/host/QuickInsights.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickUpdates.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJSValue>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>

#include <memory>
#include <optional>
#include <tuple>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;
using test::SystemSingletons;

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

#ifdef Q_OS_LINUX
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
    steps.deleteSecrets = [](SpeecherRemoval::Steps::SecretsDeleted done) { done(true, {}); };
    steps.settingsHideKeys = [] { return false; };
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
#endif

// Removal's stand-in steps where the computer has removal, as Linux does;
// none elsewhere.
std::optional<SpeecherRemoval::Steps> standInRemoval()
{
#ifdef Q_OS_LINUX
    return quietRemoval();
#else
    return std::nullopt;
#endif
}

// System, Removal and Phone with stand-ins for what they would touch on the
// computer: the virtual keyboard and removal.
struct StandInSystem : SystemSingletons {
    StandInSystem(ApplicationController *controller, FakeKeyboard &keyboard,
                  std::optional<SpeecherRemoval::Steps> removalSteps = standInRemoval())
        : SystemSingletons(controller, keyboard.system(), std::move(removalSteps))
    {
    }
};

// A QML var property as plain values.
QVariant plain(const QVariant &value)
{
    return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

// Takes the links handed to the desktop, in place of the browser.
class OpenedLinks : public QObject {
    Q_OBJECT

public:
    QList<QUrl> urls;

public slots:
    void open(const QUrl &url) { urls.append(url); }
};

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

// The UI's System, Removal and Phone singletons over core's virtual keyboard
// setup, removal and phone transfer, with stand-ins for the computer.
class QuickSystemTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

#ifdef Q_OS_LINUX
    // The keyboard is set up through the helper, then waits for the typing
    // test, and turns on only when the field gets exactly the test text.
    void theVirtualKeyboardTurnsOnOnlyAfterTheTypingTestPasses()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("notInstalled"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("fix")).toString(), QStringLiteral("setUp"));

        system.fix(QStringLiteral("virtualKeyboardHealth"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("state")).toString(), QStringLiteral("working"));
        QTRY_COMPARE(system.virtualKeyboardTest(), QStringLiteral("ready"));
        QCOMPARE(keyboard.calls, (QStringList{QStringLiteral("install"), QStringLiteral("start")}));
        QVERIFY(!SettingsStore().dictationSnapshot().output.ydotoolEnabled);

        // What arrives before a run, or other text, turns nothing on.
        system.virtualKeyboardTestReceived(VirtualKeyboardSetup::testText());
        system.runVirtualKeyboardTest();
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("typing"));
        QTRY_VERIFY(keyboard.calls.contains(QStringLiteral("type ") + VirtualKeyboardSetup::testText()));
        system.virtualKeyboardTestReceived(QStringLiteral("speecher"));
        QVERIFY(!SettingsStore().dictationSnapshot().output.ydotoolEnabled);

        system.virtualKeyboardTestReceived(VirtualKeyboardSetup::testText());
        QVERIFY(SettingsStore().dictationSnapshot().output.ydotoolEnabled);
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("off"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QCOMPARE(system.virtualKeyboardActions(), (QStringList{QStringLiteral("disable"), QStringLiteral("remove")}));

        system.disableVirtualKeyboard();
        QVERIFY(!SettingsStore().dictationSnapshot().output.ydotoolEnabled);
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("disabled"));
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("fix")).toString(), QStringLiteral("enable"));
    }

    void aTestWhoseWordsNeverArriveMisses()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;
        QCOMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("disabled"));

        // Already set up: turning it on goes straight to the test.
        system.fix(QStringLiteral("virtualKeyboardHealth"));
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("ready"));
        QVERIFY(keyboard.calls.isEmpty());
        system.runVirtualKeyboardTest();
        QTRY_COMPARE_WITH_TIMEOUT(system.virtualKeyboardTest(), QStringLiteral("missed"),
                                  VirtualKeyboardSetup::testArrivalMs * 3);
        QVERIFY(!SettingsStore().dictationSnapshot().output.ydotoolEnabled);

        // A keyboard that can't type says so, and stays off.
        keyboard.typeSucceeds = false;
        system.runVirtualKeyboardTest();
        QTRY_COMPARE(keyboardFailures(system), QVariantList{failure("type", QString())});
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("ready"));
        QVERIFY(!SettingsStore().dictationSnapshot().output.ydotoolEnabled);
    }

    // Declining the password prompt leaves everything as it was, and says why.
    void aDeclinedSetupChangesNothing()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("Authorization was declined.");
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;

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
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;

        system.removeVirtualKeyboard();
        QVERIFY(system.virtualKeyboardActions().isEmpty());
        QTRY_COMPARE(keyboardCheck(system).value(QStringLiteral("reason")).toString(), QStringLiteral("notInstalled"));
        QCOMPARE(keyboard.calls, (QStringList{QStringLiteral("stop"), QStringLiteral("remove")}));
        QVERIFY(!SettingsStore().dictationSnapshot().output.ydotoolEnabled);
        QVERIFY(keyboardFailures(system).isEmpty());

        // A helper that fails keeps the setting and names the failure.
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("pkexec failed");
        SettingsStore().setYdotoolEnabled(true);
        system.removeVirtualKeyboard();
        QTRY_COMPARE(keyboardFailures(system), QVariantList{failure("remove", QStringLiteral("pkexec failed"))});
        QVERIFY(SettingsStore().dictationSnapshot().output.ydotoolEnabled);
    }

    // Words stay on the clipboard while neither accessibility nor a ready
    // virtual keyboard gets them in, and only while Keep is on.
    void pastingOutcomeFollowsTheStepsThatCanRun()
    {
        ApplicationController controller(true);
        QVERIFY(!controller.accessibility()->enabled());
        FakeKeyboard missing;
        StandInSystem without(&controller, missing);
        const auto readySteps = [](const QuickSystem &system) {
            QStringList ready;
            for (const QVariant &step : system.deliverySteps()) {
                if (step.toMap().value(QStringLiteral("ready")).toBool()) {
                    ready << step.toMap().value(QStringLiteral("id")).toString();
                }
            }
            return ready.join(u' ');
        };
        const auto setSteps = [&controller](DeliverySteps steps) {
            controller.settings()->edit([steps](AppSettings &draft) { draft.output.steps = steps; return true; });
        };
        QCOMPARE(readySteps(without.system), QStringLiteral("keep"));
        QCOMPARE(without.system.pasteRoute(), (QStringList{"insert", "press", "keep"}));
        QCOMPARE(without.system.pastingOutcome(), QStringLiteral("clipboard"));
        // Insertion waits for accessibility, by name.
        const QVariantMap insert = without.system.deliverySteps().first().toMap();
        QCOMPARE(insert.value(QStringLiteral("waiting")).toStringList(), QStringList{QStringLiteral("accessibility")});
        // With Keep off nothing is left to hold them.
        setSteps({.insert = true, .press = true, .keep = false});
        QCOMPARE(without.system.pastingOutcome(), QStringLiteral("nothing"));
        // Copying only was the choice, so it is the outcome.
        setSteps({.insert = false, .press = false, .keep = true});
        QCOMPARE(without.system.pastingOutcome(), QStringLiteral("clipboard"));
        setSteps({});

        controller.settings()->setYdotoolEnabled(true);
        FakeKeyboard ready;
        ready.installed = YdotoolSetupState::Ready;
        StandInSystem with(&controller, ready);
        QCOMPARE(with.system.pastingOutcome(), QStringLiteral("delivers"));
    }

    // What a rule leaves of the steps, and the off step it relies on.
    void rulePlanNarrowsTheSteps()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        const auto plan = [&standIn](const char *method) { return standIn.system.rulePlan(QString::fromLatin1(method)); };
        QCOMPARE(plan("inherit").value(QStringLiteral("route")).toStringList(), (QStringList{"insert", "press", "keep"}));
        QCOMPARE(plan("direct_insert").value(QStringLiteral("route")).toStringList(), (QStringList{"insert", "keep"}));
        QCOMPARE(plan("clipboard_only").value(QStringLiteral("route")).toStringList(), QStringList{"keep"});
        QVERIFY(plan("terminal_paste").value(QStringLiteral("needs")).toString().isEmpty());
#ifdef Q_OS_LINUX
        // Usable is what of the route this computer can run: here, without
        // accessibility or a virtual keyboard, only Keep.
        QCOMPARE(plan("inherit").value(QStringLiteral("usable")).toStringList(), QStringList{"keep"});
#endif

        controller.settings()->edit([](AppSettings &draft) {
            draft.output.steps = {.insert = false, .press = true, .keep = false};
            return true;
        });
        // Skip paste keys with Insert off leaves no step at all, which it
        // doesn't rely on: it only takes Press away.
        QVERIFY(plan("direct_insert").value(QStringLiteral("route")).toStringList().isEmpty());
        QVERIFY(plan("direct_insert").value(QStringLiteral("needs")).toString().isEmpty());
        QCOMPARE(plan("clipboard_only").value(QStringLiteral("needs")).toString(), QStringLiteral("keep"));
        QVERIFY(plan("terminal_paste").value(QStringLiteral("needs")).toString().isEmpty());
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
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;

        QVERIFY(standIn.removal.report().isEmpty());
        standIn.removal.start(true);
        QVERIFY(standIn.removal.running());
        QTRY_VERIFY(!standIn.removal.running());
        const auto step = [](const char *part, const char *outcome) {
            return QVariantMap{{QStringLiteral("part"), QLatin1String(part)},
                               {QStringLiteral("outcome"), QLatin1String(outcome)},
                               {QStringLiteral("error"), QString()}};
        };
        QCOMPARE(standIn.removal.report().value(QStringLiteral("steps")).toList(),
                 (QVariantList{step("shortcutRegistration", "removed"), step("settings", "kept")}));
        QCOMPARE(standIn.removal.report().value(QStringLiteral("files")).toMap().value(QStringLiteral("removed")).toStringList(),
                 QStringList{});
        QVERIFY(!standIn.removal.report().value(QStringLiteral("settingsDeleted")).toBool());
    }

    // Once removal has deleted the settings, an edit made before quitting
    // doesn't bring the file back, whether it goes through a snapshot or a
    // setter.
    void deletedSettingsStayDeleted()
    {
        ApplicationController controller(true);
        const auto kept = keepSettingsHere(*controller.settings());
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        StandInSystem standIn(&controller, keyboard, steps);
        QuickSystem &system = standIn.system;
        settings.set(QStringLiteral("themeControl"), QStringLiteral("dark"));
        const QString file = controller.settings()->raw().fileName();
        controller.settings()->raw().sync();
        QVERIFY(QFile::exists(file));

        standIn.removal.start(false);
        QTRY_VERIFY(!standIn.removal.running());
        QVERIFY(standIn.removal.report().value(QStringLiteral("settingsDeleted")).toBool());
        QVERIFY(!QFile::exists(file));

        settings.set(QStringLiteral("themeControl"), QStringLiteral("light"));
        controller.settings()->setYdotoolEnabled(true);
        QCoreApplication::processEvents();
        controller.settings()->raw().sync();
        QVERIFY(!QFile::exists(file));
        QVERIFY(controller.settings()->dictationSnapshot().output.ydotoolEnabled);
        // A reader made afterwards, as a provider check makes one.
        { SettingsStore reader; Q_UNUSED(reader.dictationSnapshot().output.ydotoolEnabled); }
        QVERIFY(!QFile::exists(file));
    }

    // A keyboard setup under way finishes, saving what it saves, before
    // removal starts and deletes the settings.
    void removalWaitsForTheKeyboardSetup()
    {
        ApplicationController controller(true);
        const auto kept = keepSettingsHere(*controller.settings());
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        QString keyboardAtStart;
        QuickSystem *started = nullptr;
        steps.removeDesktopIntegration = [&keyboardAtStart, &started] {
            keyboardAtStart = keyboardCheck(*started).value(QStringLiteral("state")).toString();
            return DesktopIntegrationRemoval{};
        };
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        StandInSystem standIn(&controller, keyboard, steps);
        QuickSystem &system = standIn.system;
        started = &system;
        const QString file = controller.settings()->raw().fileName();

        system.fixFromSetup(QStringLiteral("virtualKeyboardHealth"));
        standIn.removal.start(false);
        QVERIFY(standIn.removal.running());
        QTRY_VERIFY(!standIn.removal.running());
        QVERIFY(!keyboardAtStart.isEmpty());
        QVERIFY(keyboardAtStart != QLatin1String("working"));
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("off"));
        QCoreApplication::processEvents();
        QVERIFY(!QFile::exists(file));
    }

    // While removal runs, turning the keyboard off saves nothing.
    void theKeyboardStaysOnDuringRemoval()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        controller.settings()->setYdotoolEnabled(true);

        standIn.removal.start(true);
        standIn.system.disableVirtualKeyboard();

        QVERIFY(controller.settings()->dictationSnapshot().output.ydotoolEnabled);
        QTRY_VERIFY(!standIn.removal.running());
    }

    // Closing Main while removal runs quits once removal has deleted the
    // settings, and from then on Main holds only the report.
    void closingMainBeforeRemovalFinishesQuits()
    {
        ApplicationController controller(true);
        const auto kept = keepSettingsHere(*controller.settings());
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QuickUpdates updates(&controller, controller.updates());
        QuickModels models(&controller);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        QuickApps apps(&controller);
        // Home reads Audio for its microphone.
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        // The window and General read Appearance, as the host always has it.
        QuickAppearance appearance(&controller);
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        StandInSystem standIn(&controller, keyboard, steps);
        QuickSystem &system = standIn.system;
        QSignalSpy quit(&controller, &ApplicationController::quitRequested);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/Main.qml")));
        const std::unique_ptr<QObject> created(component.create());
        auto *window = qobject_cast<QQuickWindow *>(created.get());
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window));

        standIn.removal.start(false);
        window->hide();
        QVERIFY(standIn.removal.running());
        QCOMPARE(quit.count(), 0);
        QTRY_COMPARE(quit.count(), 1);
        auto *report = window->findChild<QQuickItem *>(QStringLiteral("removalReport"));
        QVERIFY(report);
        QVERIFY(report->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("sidebar"))->isVisible());
    }

#endif

    // The code exists only between start and stop, and a new one replaces it.
    void thePhoneCodeLivesWhileItShows()
    {
        ApplicationController controller(true);
        QuickPhone phone(&controller);
        QCOMPARE(phone.state(), QString());

        phone.start();
        const QString first = phone.link();
        if (phone.state() == QLatin1String("noNetwork")) {
            QVERIFY(first.isEmpty());
        } else {
            QCOMPARE(phone.state(), QStringLiteral("waiting"));
            QVERIFY(first.startsWith(QStringLiteral("speecher://import?")));
            phone.start();
            QVERIFY(phone.link() != first);
        }
        phone.stop();
        QCOMPARE(phone.state(), QString());
        QCOMPARE(phone.link(), QString());
        QCOMPARE(phone.bundle().value(QStringLiteral("terms")).toInt(), 0);
    }

    // A page that hides takes its code with it; showing it again starts
    // without one, and the next code is new.
    void hidingMainStopsThePhoneCode()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        QuickPhone &phone = standIn.phone;
        // General clears Insights' history, as the host always has it.
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        QuickApps apps(&controller);
        QuickNavigation navigation(nullptr);
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
        QVERIFY(!phone.state().isEmpty());
        const QString first = phone.link();
        window->hide();
        QCOMPARE(phone.state(), QString());
        QVERIFY(!page->property("_phoneOpen").toBool());

        window->show();
        QCOMPARE(phone.state(), QString());
        QMetaObject::invokeMethod(page, "showPhoneCode", Q_ARG(QVariant, true));
        QVERIFY(!phone.state().isEmpty());
        if (!first.isEmpty()) {
            QVERIFY(phone.link() != first);
        }
    }

#ifdef Q_OS_LINUX
    // A row follows its check as it changes, and a fix that fails says why
    // in the row, as the setup assistant's row does.
    void aHealthRowFollowsItsCheck()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        keyboard.helperSucceeds = false;
        keyboard.helperError = QStringLiteral("Authorization was declined.");
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;
        QuickNavigation navigation(nullptr);
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

    // Setup words where the words end up from the one outcome Home and
    // Pasting show, which follows its draft: the Pasting step passes once a
    // step that is on gets them into the app, and is needed only while they
    // get nowhere at all. Words left on the clipboard leave it open but not
    // needed. The saved switches stay until setup saves. Setup counts as
    // complete only once no needed step is open, and no step's check holds
    // back another.
    void setupPastingFollowsTheOutcomeOfItsDraft()
    {
        ApplicationController controller(true);
        QVERIFY(!controller.accessibility()->enabled());
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::NeedsSignOut;
        keyboard.managed = true;
        StandInSystem standIn(&controller, keyboard);
        // Setup's choices read Models and Audio, as the host always has them.
        QuickModels models(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(component.create());
        QVERIFY2(choices, qPrintable(component.errorString()));
        const auto step = [&choices](const char *id) {
            for (const QVariant &step : plain(choices->property("steps")).toList()) {
                if (step.toMap().value(QStringLiteral("id")) == QLatin1String(id)) {
                    return step.toMap();
                }
            }
            return QVariantMap();
        };
        const auto delivery = [&step] { return step("delivery"); };

        // A keyboard waiting for the next sign-in can't press the keys yet,
        // so the words stay on the clipboard, as Home says.
        QCOMPARE(standIn.system.pastingOutcome(), QStringLiteral("clipboard"));
        QVERIFY(!delivery().value(QStringLiteral("done")).toBool());
        QVERIFY(!delivery().value(QStringLiteral("needed")).toBool());
        QCOMPARE(delivery().value(QStringLiteral("verdict")).toString(), QStringLiteral("Your words stay on the clipboard"));

        // Keep off in the draft leaves nothing, in setup and in System
        // alike, though the saved switches still keep the words.
        standIn.settings.set(QStringLiteral("keepOnClipboard"), false);
        QVERIFY(delivery().value(QStringLiteral("needed")).toBool());
        QCOMPARE(delivery().value(QStringLiteral("verdict")).toString(), QStringLiteral("Nothing can deliver your words"));
        QCOMPARE(standIn.system.pastingOutcome(), QStringLiteral("nothing"));
        QVERIFY(controller.settings()->dictationSnapshot().output.steps.keep);

        // A keyboard that is ready gets them in with Press alone, Keep off.
        keyboard.installed = YdotoolSetupState::Ready;
        controller.settings()->setYdotoolEnabled(true);
        standIn.virtualKeyboard.refresh();
        standIn.settings.set(QStringLiteral("insertThroughAccessibility"), false);
        QTRY_VERIFY(delivery().value(QStringLiteral("done")).toBool());
        QVERIFY(!delivery().value(QStringLiteral("needed")).toBool());
        QCOMPARE(delivery().value(QStringLiteral("verdict")).toString(), QStringLiteral("Paste keys"));

        // Refinement and the shortcut are never needed. Speech and the
        // microphone are, until they pass, and keep setup from being
        // complete; nothing else stops it.
        QVERIFY(!step("refinement").value(QStringLiteral("needed")).toBool());
        QVERIFY(!step("shortcut").value(QStringLiteral("needed")).toBool());
        QVERIFY(step("microphone").value(QStringLiteral("needed")).toBool());
        QVERIFY(!choices->property("complete").toBool());
    }

    // System's delivery steps and outcome are the settings as Settings
    // shows them: with a draft's switches over the saved ones until it is
    // dropped, and saved once it is saved. A row the draft doesn't hold
    // saves at once, and a save from elsewhere shows under the draft.
    void deliveryFollowsTheSettingsDraft()
    {
        ApplicationController controller(true);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        QuickSettings &settings = standIn.settings;
        const auto saved = [&controller] { return controller.settings()->dictationSnapshot(); };
        QCOMPARE(standIn.system.pastingOutcome(), QStringLiteral("clipboard"));

        settings.startDraft({QStringLiteral("keepOnClipboard")});
        settings.set(QStringLiteral("keepOnClipboard"), false);
        QCOMPARE(standIn.system.pastingOutcome(), QStringLiteral("nothing"));
        QVERIFY(!settings.values().value(QStringLiteral("keepOnClipboard")).toBool());
        QVERIFY(saved().output.steps.keep);
        settings.dropDraft();
        QCOMPARE(standIn.system.pastingOutcome(), QStringLiteral("clipboard"));
        QVERIFY(settings.values().value(QStringLiteral("keepOnClipboard")).toBool());

        settings.startDraft({QStringLiteral("keepOnClipboard")});
        settings.set(QStringLiteral("keepOnClipboard"), false);
        settings.set(QStringLiteral("outputFormat"), QStringLiteral("html"));
        QCOMPARE(saved().output.format, OutputFormat::Html);
        controller.settings()->edit([](AppSettings &draft) {
            draft.output.steps = draft.output.steps.withStep(DeliveryStep::Press, false);
            return true;
        });
        QVERIFY(!settings.values().value(QStringLiteral("pressPasteKeys")).toBool());
        QVERIFY(!settings.values().value(QStringLiteral("keepOnClipboard")).toBool());
        settings.saveDraft();
        QVERIFY((saved().output.steps == DeliverySteps{.insert = true, .press = false, .keep = false}));
        QCOMPARE(standIn.system.pastingOutcome(), QStringLiteral("nothing"));
    }

    // Home shows pasting as one row, with what is missing by name: the
    // virtual keyboard never has its own. Nothing that can deliver keeps
    // dictation from being ready; the clipboard holding the words doesn't.
    void homeShowsPastingAsOneRow()
    {
        ApplicationController controller(true);
        QVERIFY(!controller.accessibility()->enabled());
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        QuickDictation dictation(&controller);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        QuickApps apps(&controller);
        QuickModels models(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const std::unique_ptr<QObject> window = createQml(engine, R"(
            import QtQuick
            Window { width: 1000; height: 900; visible: true; HomePage { anchors.fill: parent } }
        )", "qrc:/qt/qml/Speecher/App/pages/Test.qml");
        QVERIFY(window);
        auto *session = window->findChild<QObject *>(QStringLiteral("homeSession"));
        QVERIFY(session);
        const auto pasting = [session] { return plain(session->property("pastingItem")).toMap(); };

        QCOMPARE(pasting().value(QStringLiteral("status")).toString(), QStringLiteral("Your words stay on the clipboard"));
        QCOMPARE(pasting().value(QStringLiteral("help")).toString(),
                 QStringLiteral("Needs Desktop accessibility and Virtual keyboard."));
        QVERIFY(!session->property("pastingNeeded").toBool());

        settings.set(QStringLiteral("keepOnClipboard"), false);
        QCOMPARE(pasting().value(QStringLiteral("status")).toString(), QStringLiteral("Nothing can deliver your words"));
        QVERIFY(session->property("pastingNeeded").toBool());
        QVERIFY(session->property("blocked").toBool());
        QVERIFY(session->property("caption").toString().contains(QStringLiteral("a way to deliver your words")));
    }

#endif

#ifndef Q_OS_LINUX
    // Off Linux there is no virtual keyboard to check or set up, and no
    // removal: General and setup leave them out.
    void theVirtualKeyboardAndRemovalAreLinuxOnly()
    {
        ApplicationController controller(true);
        const SystemSingletons onThisComputer(&controller);
        const QuickSystem &system = onThisComputer.system;
        QStringList checkIds;
        for (const QVariant &check : system.checks()) {
            checkIds.append(check.toMap().value(QStringLiteral("id")).toString());
        }
        QCOMPARE(checkIds, (QStringList{QStringLiteral("microphoneAccess"), QStringLiteral("desktopAccessibility"),
                                        QStringLiteral("globalShortcuts")}));
        QVERIFY(!system.hasVirtualKeyboard());
        QCOMPARE(system.virtualKeyboardTest(), QStringLiteral("off"));
        QVERIFY(system.virtualKeyboardActions().isEmpty());
        QVERIFY(!system.canRemove());
    }
#endif

    // Only a web link reaches the desktop, never a path, a file share or
    // another scheme: release notes come from the network.
    void openTakesWebLinksOnly()
    {
        ApplicationController controller(true);
        SystemSingletons onThisComputer(&controller);
        OpenedLinks opened;
        const QStringList schemes{QStringLiteral("https"), QStringLiteral("file"), QStringLiteral("smb"),
                                  QStringLiteral("c"), QStringLiteral("speecher-test")};
        for (const QString &scheme : schemes) {
            QDesktopServices::setUrlHandler(scheme, &opened, "open");
        }
        const auto restore = qScopeGuard([&schemes] {
            for (const QString &scheme : schemes) {
                QDesktopServices::unsetUrlHandler(scheme);
            }
        });
        const QString path = QDir::homePath() + QStringLiteral("/notes.txt");

        onThisComputer.system.open(QStringLiteral("https://example.com/notes"));
        for (const QString &refused : {path, QStringLiteral("//server/share/payload"),
                                       QStringLiteral("C:/Windows/System32/cmd.exe"), QStringLiteral("smb://host/share"),
                                       QStringLiteral("file:///etc/hosts"), QStringLiteral("speecher-test:run"),
                                       QStringLiteral("https:no-host")}) {
            QTest::ignoreMessage(QtWarningMsg, qPrintable(QStringLiteral("System.open: not a web link: ") + refused));
            onThisComputer.system.open(refused);
        }

        QCOMPARE(opened.urls, QList<QUrl>{QUrl(QStringLiteral("https://example.com/notes"))});
    }

    // What this computer lacks, the virtual keyboard and removal off Linux,
    // is gone alike from its pages, search and setup: Home's keyboard check
    // and Remove Speecher, which General says how to do instead, Pasting's
    // keyboard row and its Virtual keyboard only choice, which leaves Paste
    // with nothing to choose.
    void whatTheComputerLacksIsGoneFromPagesSearchAndSetup()
    {
#ifdef Q_OS_LINUX
        const bool onLinux = true;
#else
        const bool onLinux = false;
#endif
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard virtualKeyboard;
        StandInSystem standIn(&controller, virtualKeyboard);
        // The pages and setup read Dictation, Insights, Models and Audio, as
        // the host always has them.
        QuickDictation dictation(&controller);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        QuickApps apps(&controller);
        QuickModels models(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;

        // Each row's id and whether it shows, on the page in a window.
        const auto rowsShown = [&engine](const QByteArray &page) {
            const std::unique_ptr<QObject> created = createQml(engine, R"(
                import QtQuick
                Window { width: 1000; height: 900; visible: true; )" + page + R"( { anchors.fill: parent } }
            )", "qrc:/qt/qml/Speecher/App/pages/Test.qml");
            auto *window = qobject_cast<QQuickWindow *>(created.get());
            QHash<QString, bool> shown;
            if (!window || !QTest::qWaitForWindowExposed(window)) {
                return shown;
            }
            QList<QQuickItem *> items{window->contentItem()};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                items += item->childItems();
                const QString rowId = item->property("rowId").toString();
                if (!rowId.isEmpty()) {
                    shown[rowId] = item->isVisible();
                }
            }
            return shown;
        };
        // Home lists the checks while one fails, as the missing microphone
        // does here, but the virtual keyboard only as part of Pasting.
        const QHash<QString, bool> home = rowsShown("HomePage");
        QVERIFY(!home.value(QStringLiteral("virtualKeyboardHealth")));
        // Microphone access is part of the Microphone row.
        QVERIFY(!home.value(QStringLiteral("microphoneAccess")));
        const QHash<QString, bool> general = rowsShown("GeneralPage");
        QCOMPARE(general.value(QStringLiteral("removeSpeecher")), onLinux);
        QCOMPARE(general.value(QStringLiteral("removeSpeecherGuide")), !onLinux);
        // Every platform has the three delivery steps.
        const QHash<QString, bool> pasting = rowsShown("PastingPage");
        for (const char *step : {"insertThroughAccessibility", "pressPasteKeys", "keepOnClipboard"}) {
            QVERIFY2(pasting.value(QLatin1String(step)), step);
        }

        const std::unique_ptr<QObject> probe = createQml(engine, R"(
            import QtQuick
            import Speecher.Backend
            import Speecher.Wording
            QtObject {
                readonly property var keyboard: Schema.search("keyboard", 50, need => System[need]).map(r => r.id)
                readonly property var remove: Schema.search("remove speecher", 50, need => System[need]).map(r => r.id)
            }
        )");
        QVERIFY(probe);
        const QStringList keyboard = plain(probe->property("keyboard")).toStringList();
        // The step row stands for its check in search.
        QVERIFY(!keyboard.contains(QStringLiteral("virtualKeyboardHealth")));
        QVERIFY(keyboard.contains(QStringLiteral("pressPasteKeys")));
        const QStringList remove = plain(probe->property("remove")).toStringList();
        QCOMPARE(remove.contains(QStringLiteral("removeSpeecher")), onLinux);
        QCOMPARE(remove.contains(QStringLiteral("removeSpeecherGuide")), !onLinux);

        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(component.create());
        QVERIFY2(choices, qPrintable(component.errorString()));
        QStringList stepIds;
        for (const QVariant &step : plain(choices->property("steps")).toList()) {
            stepIds.append(step.toMap().value(QStringLiteral("id")).toString());
        }
        // Seven steps on every platform: Pasting asks for accessibility itself.
        QCOMPARE(stepIds, (QStringList{"welcome", "transcription", "microphone", "delivery", "refinement", "shortcut", "ready"}));
    }

    // Pasting lists only the Apps and App Types whose Pasting choice
    // narrows the steps: on a fresh install the terminals' keys, where they
    // differ, then what Apps sets, each with what it leaves of the steps.
    void pastingListsOnlyTheChoicesThatDiffer()
    {
        ApplicationController controller(true);
        // The page reads Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        QuickModels models(&controller);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        QuickApps apps(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/PastingPage.qml")));
        const std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        QObject *section = page->findChild<QObject *>(QStringLiteral("appsPasteDifferently"));
        QVERIFY(section);
        const auto listed = [section] {
            QStringList ids;
            for (const QVariant &entry : plain(section->property("entries")).toList()) {
                ids << entry.toMap().value(QStringLiteral("id")).toString();
            }
            return ids;
        };
        QCOMPARE(listed(), terminalKeysDiffer ? QStringList{QStringLiteral("apptype:terminal")} : QStringList());

        QCOMPARE(apps.updateType(QStringLiteral("browser"), {{QStringLiteral("pasting"), QStringLiteral("direct_insert")}}), QString());
        const QString kate = apps.add({{QStringLiteral("name"), QStringLiteral("Kate")},
                                       {QStringLiteral("ids"), QStringList{QStringLiteral("org.kde.kate")}}})
                                 .value(QStringLiteral("id")).toString();
        QCOMPARE(apps.update(kate, {{QStringLiteral("pasting"), QStringLiteral("clipboard_only")}}), QString());
        QStringList expected{QStringLiteral("apptype:browser"), QStringLiteral("appentry:") + kate};
        if (terminalKeysDiffer) {
            expected.prepend(QStringLiteral("apptype:terminal"));
        }
        QCOMPARE(listed(), expected);

        // What a choice does here, as Apps' editor and this list say it.
        QObject *schema = engine.singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        QVERIFY(schema);
        const auto result = [&](const char *method, bool terminalWins = false) {
            QJSValue pasting = engine.toScriptValue(schema).property(QStringLiteral("pasting"));
            const QVariant plan = standIn.system.rulePlan(QString::fromLatin1(method));
            return pasting.property(QStringLiteral("ruleResult"))
                .call({engine.toScriptValue(plan), QString::fromLatin1(method), terminalWins})
                .toString();
        };
        QCOMPARE(result("clipboard_only"), QStringLiteral("Clipboard only"));
#ifdef Q_OS_LINUX
        // Without accessibility only the copy it keeps can run here.
        QCOMPARE(result("direct_insert"), QStringLiteral("Clipboard only. Insert can't run here yet"));
#endif
        QCOMPARE(result("clipboard_only", true), QStringLiteral("Clipboard only. Apps in a terminal paste like Terminals"));

        // A choice that relies on an off step, and leaves none, says so.
        settings.set(QStringLiteral("keepOnClipboard"), false);
        settings.set(QStringLiteral("insertThroughAccessibility"), false);
        QCOMPARE(result("clipboard_only"), QStringLiteral("No delivery: Keep is off"));
    }

#ifdef Q_OS_LINUX
    // With Press the only step that runs, skipping the paste keys leaves
    // nothing that can run here, whatever Insert's switch says; and a ready
    // Press doesn't hide that selection edits can't read the selection.
    void withOnlyPressRunningPastingSaysWhatIsMissing()
    {
        ApplicationController controller(true);
        QVERIFY(!controller.accessibility()->enabled());
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::Ready;
        keyboard.managed = true;
        controller.settings()->setYdotoolEnabled(true);
        StandInSystem standIn(&controller, keyboard);
        QuickModels models(&controller);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        QuickApps apps(&controller);
        QuickNavigation navigation(nullptr);
        settings.set(QStringLiteral("keepOnClipboard"), false);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/PastingPage.qml")));
        const std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        // What a Pasting choice does here, as Apps and Pasting say it.
        QObject *schema = engine.singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        QVERIFY(schema);
        const auto result = [&](const QString &method) {
            const QJSValue pasting = engine.toScriptValue(schema).property(QStringLiteral("pasting"));
            return pasting.property(QStringLiteral("ruleResult"))
                .call({engine.toScriptValue(standIn.system.rulePlan(method)), method, false})
                .toString();
        };
        QCOMPARE(result(QStringLiteral("direct_insert")), QStringLiteral("No delivery: nothing it allows can run here"));
        QCOMPARE(result(QStringLiteral("terminal_paste")), QStringLiteral("Press Ctrl+Shift+V. Insert can't run here yet"));
        QCOMPARE(page->property("_selectionLimit").toString(),
                 QStringLiteral("Reading the selection needs Desktop accessibility, which Insert above turns on."));
    }
#endif
};

int runQuickSystemTests(int argc, char **argv)
{
    QuickSystemTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_system.moc"
