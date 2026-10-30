#include "common/test_suites.h"
#include "common/test_system_singletons.h"

#include "app/AccessibilityMonitor.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickAppearance.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickDictation.h"
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
    steps.deleteSecrets = [](QString *) { return true; };
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

    // Only the setup assistant's own setup makes the output method Automatic,
    // as the assistant did before.
    void setupsKeyboardPastesAutomatically()
    {
        ApplicationController controller(true);
        const QString ydotool = QString::fromLatin1(OutputMethod::Ydotool);
        // The output method after setting up a keyboard that isn't there yet.
        const auto afterSetUp = [&controller, &ydotool](bool fromSetup, bool helperSucceeds) {
            controller.settings()->edit([&](AppSettings &draft) { draft.output.method = ydotool; return true; });
            FakeKeyboard keyboard;
            keyboard.helperSucceeds = helperSucceeds;
            StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;
            const QString id = QStringLiteral("virtualKeyboardHealth");
            fromSetup ? system.fixFromSetup(id) : system.fix(id);
            const bool finished = QTest::qWaitFor([&system] {
                return keyboardCheck(system).value(QStringLiteral("state")).toString() != QLatin1String("working");
            });
            return finished ? SettingsStore().dictationSnapshot().output.method : QStringLiteral("still setting up");
        };
        QCOMPARE(afterSetUp(false, true), ydotool);
        QCOMPARE(afterSetUp(true, true), QString::fromLatin1(OutputMethod::Automatic));
        QCOMPARE(afterSetUp(true, false), ydotool);
    }

    // Words stay on the clipboard while neither accessibility nor a ready
    // virtual keyboard gets them in.
    void wordsStayCopiedUntilTheKeyboardIsReady()
    {
        ApplicationController controller(true);
        QVERIFY(!controller.accessibility()->enabled());
        FakeKeyboard missing;
        StandInSystem without(&controller, missing);
        QCOMPARE(without.system.pasteNeed(), QStringLiteral("either"));
        QVERIFY(without.system.wordsStayCopied());

        controller.settings()->setYdotoolEnabled(true);
        FakeKeyboard ready;
        ready.installed = YdotoolSetupState::Ready;
        StandInSystem with(&controller, ready);
        QVERIFY(!with.system.wordsStayCopied());
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
        FakeKeyboard keyboard;
        SpeecherRemoval::Steps steps = quietRemoval();
        QString methodAtStart;
        steps.removeDesktopIntegration = [&controller, &methodAtStart] {
            methodAtStart = controller.settings()->dictationSnapshot().output.method;
            return DesktopIntegrationRemoval{};
        };
        steps.deleteSettings = [&controller](QString *error) { return controller.settings()->deleteAll(error); };
        StandInSystem standIn(&controller, keyboard, steps);
        QuickSystem &system = standIn.system;
        controller.settings()->edit([](AppSettings &draft) {
            draft.output.method = QStringLiteral("clipboard");
            return true;
        });
        const QString file = controller.settings()->raw().fileName();

        system.fixFromSetup(QStringLiteral("virtualKeyboardHealth"));
        standIn.removal.start(false);
        QVERIFY(standIn.removal.running());
        QTRY_VERIFY(!standIn.removal.running());
        QCOMPARE(methodAtStart, QString::fromLatin1(OutputMethod::Automatic));
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
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QuickUpdates updates(&controller, controller.updates());
        QuickModels models(&controller);
        QuickInsights insights(&controller);
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

    // Setup goes on past a check that is as far as this computer gets, and
    // the check still says so.
    void setupSettlesForWhatTheComputerAllows()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        keyboard.installed = YdotoolSetupState::NeedsSignOut;
        keyboard.managed = true;
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;
        // Setup's choices read Models and Audio, as the host always has them.
        QuickModels models(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
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

    // Only a web link or an absolute path reaches the desktop: release notes
    // come from the network.
    void openTakesWebLinksAndPathsOnly()
    {
        ApplicationController controller(true);
        SystemSingletons onThisComputer(&controller);
        OpenedLinks opened;
        const QStringList schemes{QStringLiteral("https"), QStringLiteral("file"), QStringLiteral("smb"),
                                  QStringLiteral("speecher-test")};
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
        onThisComputer.system.open(path);
        for (const char *refused : {"smb://host/share", "file:///etc/hosts", "speecher-test:run", "https:no-host"}) {
            QTest::ignoreMessage(QtWarningMsg,
                                 qPrintable(QStringLiteral("System.open: not a web link or an absolute path: ")
                                            + QLatin1String(refused)));
            onThisComputer.system.open(QLatin1String(refused));
        }

        QCOMPARE(opened.urls, (QList<QUrl>{QUrl(QStringLiteral("https://example.com/notes")), QUrl::fromLocalFile(path)}));
    }

    // What this computer lacks, the virtual keyboard and removal off Linux,
    // is gone alike from its pages, search and setup: General's keyboard
    // row and Remove Speecher, Pasting's keyboard row and its Virtual
    // keyboard only choice, which leaves Paste with nothing to choose.
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
        // General reads Insights, Pasting and setup Models, setup Audio, as
        // the host always has them.
        QuickInsights insights(&controller);
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
        const QHash<QString, bool> permissions = rowsShown("PermissionsPage");
        QCOMPARE(permissions.value(QStringLiteral("virtualKeyboardHealth")), onLinux);
        QVERIFY(permissions.value(QStringLiteral("microphoneAccess")));
        QCOMPARE(rowsShown("GeneralPage").value(QStringLiteral("removeSpeecher")), onLinux);
        const QHash<QString, bool> pasting = rowsShown("PastingPage");
        QCOMPARE(pasting.value(QStringLiteral("virtualKeyboard")), onLinux);
        QCOMPARE(pasting.value(QStringLiteral("outputMethod")), onLinux);
        QVERIFY(pasting.value(QStringLiteral("globalPasteRule")));

        const std::unique_ptr<QObject> probe = createQml(engine, R"(
            import QtQuick
            import Speecher.Backend
            import Speecher.Wording
            QtObject {
                readonly property var keyboard: Schema.search("keyboard", 50, need => System[need]).map(r => r.id)
                readonly property var remove: Schema.search("remove", 50, need => System[need]).map(r => r.id)
                readonly property var pasteWith: Schema.options(Schema.row("outputMethod"), need => System[need]).map(o => o.id)
            }
        )");
        QVERIFY(probe);
        const QStringList keyboard = plain(probe->property("keyboard")).toStringList();
        QCOMPARE(keyboard.contains(QStringLiteral("virtualKeyboardHealth")), onLinux);
        QCOMPARE(keyboard.contains(QStringLiteral("virtualKeyboard")), onLinux);
        QCOMPARE(keyboard.contains(QStringLiteral("outputMethod")), onLinux);
        QCOMPARE(plain(probe->property("remove")).toStringList().contains(QStringLiteral("removeSpeecher")), onLinux);
        const QStringList pasteWith = onLinux ? QStringList{QStringLiteral("automatic"), QStringLiteral("ydotool")}
                                              : QStringList{QStringLiteral("automatic")};
        QCOMPARE(plain(probe->property("pasteWith")).toStringList(), pasteWith);

        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        const std::unique_ptr<QObject> choices(component.create());
        QVERIFY2(choices, qPrintable(component.errorString()));
        QStringList stepIds;
        for (const QVariant &step : plain(choices->property("steps")).toList()) {
            stepIds.append(step.toMap().value(QStringLiteral("id")).toString());
        }
        QCOMPARE(stepIds.contains(QStringLiteral("delivery")), onLinux);
        QVERIFY(stepIds.contains(QStringLiteral("accessibility")));
    }

    // The App rules page's app type rows need accessibility to know the app.
    void appTypeRowsFollowAccessibility()
    {
        ApplicationController controller(true);
        // The pages read Library, which works through this controller.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        FakeKeyboard keyboard;
        StandInSystem standIn(&controller, keyboard);
        QuickSystem &system = standIn.system;
        // The page's Library reads Models, as the host always has one.
        QuickModels models(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/AppRulesPage.qml")));
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
