#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/CommandLine.h"
#ifdef Q_OS_LINUX
#include "app/LinuxComposition.h"
#endif
#include "app/PlatformComposition.h"
#include "core/LearnedCorrection.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "platform/CorrectionDiff.h"
#include "platform/GlobalShortcutBinder.h"
#ifdef Q_OS_LINUX
#include "platform/LinuxDesktopIntegration.h"
#include "ui/SetupAssistant.h"
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#include "ui/setup/SetupPages.h"
#endif

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QPalette>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#ifdef SPEECHER_WITH_KASSISTANT
#include <KPageWidget>
#endif
#include <QTest>

#include <memory>
#include <utility>

using namespace speecher;

namespace {

class FakeGlobalShortcutBinder final : public GlobalShortcutBinder {
public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    bool supported() const override
    {
        return shortcutsSupported;
    }

    bool supportKnown() const override { return shortcutSupportKnown; }

    bool usesDesktopShortcutChooser() const override { return desktopChooser; }

    QString unsupportedReason() const override
    {
        return unsupported;
    }

    void bind() override
    {
        bindCount += 1;
    }

    QKeySequence shortcut() const override
    {
        return m_shortcut;
    }

    bool setShortcut(const QKeySequence &shortcut, QString *error) override
    {
        if (!setShortcutError.isEmpty()) {
            if (error) {
                *error = setShortcutError;
            }
            return false;
        }
        if (shortcut.isEmpty()) {
            if (error) {
                *error = QStringLiteral("fake binder rejects an empty sequence");
            }
            return false;
        }
        m_shortcut = shortcut;
        return true;
    }

    void registerShortcut() override
    {
        registerCount += 1;
    }

    void publishShortcut(const QKeySequence &shortcut)
    {
        m_shortcut = shortcut;
        emit bindingChanged();
    }

    void publishRegistrationResult(bool bound, const QString &detail)
    {
        emit registrationFinished(bound, detail);
    }

    void publishSupport(bool known, bool supported)
    {
        shortcutSupportKnown = known;
        shortcutsSupported = supported;
        emit supportChanged();
    }

    int bindCount = 0;
    int registerCount = 0;
    bool shortcutSupportKnown = true;
    bool shortcutsSupported = true;
    bool desktopChooser = false;
    QString unsupported;
    QString setShortcutError;

private:
    QKeySequence m_shortcut;
};

// Answers for itself everything the seam added, and delegates the ports it does
// not care about to the composition the platform already provides.
class FakePlatformComposition final : public PlatformComposition {
public:
    explicit FakePlatformComposition(std::shared_ptr<const PlatformComposition> delegate)
        : m_delegate(std::move(delegate))
    {
    }

    QString outputSummary() const override
    {
        return QStringLiteral("Fake: nothing is delivered");
    }

    QString ipcListenName() const override
    {
        return m_delegate->ipcListenName();
    }

    QStringList ipcConnectCandidates() const override
    {
        return m_delegate->ipcConnectCandidates();
    }

    QString detachedExecutablePath() const override
    {
        return m_delegate->detachedExecutablePath();
    }

    QList<AudioInputDeviceInfo> availableAudioInputDevices() const override
    {
        return {{QStringLiteral("fake-device"), QStringLiteral("Fake microphone")}};
    }

    AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const override
    {
        return m_delegate->createAudioInput(settings, parent);
    }

    MediaController *createMediaController(QObject *parent) const override
    {
        return m_delegate->createMediaController(parent);
    }

    TargetProvider *createTargetProvider(QObject *parent) const override
    {
        return m_delegate->createTargetProvider(parent);
    }

    ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const override
    {
        return m_delegate->createScreenshotContextProvider(parent);
    }

    TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const override
    {
        return m_delegate->createTextDelivery(targetProvider, parent);
    }

    PopupPositioner *createPopupPositioner(QObject *parent) const override
    {
        return m_delegate->createPopupPositioner(parent);
    }

    GlobalShortcutBinder *createGlobalShortcutBinder(QObject *parent) const override
    {
        binder = new FakeGlobalShortcutBinder(parent);
        return binder;
    }

    AccessibilityState accessibilityState() const override
    {
        return accessibility;
    }

    void watchAccessibilityChanges(QObject *, std::function<void()> refresh) const override
    {
        accessibilityRefresh = std::move(refresh);
    }

    bool requestAccessibility(QString *) const override
    {
        return true;
    }

    bool enableAccessibilityPermanently(QString *) const override
    {
        return true;
    }

    mutable FakeGlobalShortcutBinder *binder = nullptr;
    mutable AccessibilityState accessibility{true, true, false};
    mutable std::function<void()> accessibilityRefresh;

private:
    std::shared_ptr<const PlatformComposition> m_delegate;
};

// Records what the controller asks of a user interface, so the seam can be
// checked without a window on screen.
class FakeAppFrontEnd final : public AppFrontEnd {
public:
    void showMainWindow() override
    {
        calls << QStringLiteral("showMainWindow");
    }

    void showSettingsWindow() override
    {
        calls << QStringLiteral("showSettingsWindow");
    }

    void showSetupAssistant(SetupAssistantPage page) override
    {
        calls << (page == SetupAssistantPage::All
                      ? QStringLiteral("showSetupAssistant")
                      : QStringLiteral("showSetupAssistant GlobalShortcut"));
    }

    bool captureMainWindow(const QString &path) override
    {
        calls << QStringLiteral("captureMainWindow ") + path;
        return true;
    }

    void showDictationError(const QString &message) override
    {
        calls << QStringLiteral("showDictationError ") + message;
    }

    void alert() override
    {
        calls << QStringLiteral("alert");
    }

    QStringList calls;
};

} // namespace

class PlatformCompositionTests : public QObject {
    Q_OBJECT

private slots:
#ifdef Q_OS_LINUX
    void guiLaunchKeepsRunningAfterLastWindowCloses()
    {
        QVERIFY(!quitOnLastWindowClosed(LaunchMode::RunGui));
        QVERIFY(!quitOnLastWindowClosed(LaunchMode::RunDaemon));
    }

    void quitIsAClientCommand()
    {
        const CommandLineDecision decision = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("quit")}, {});
        QCOMPARE(decision.mode, LaunchMode::RunCli);
        QCOMPARE(decision.ipcCommand, QStringLiteral("quit"));

        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QSignalSpy requested(&controller, &ApplicationController::quitRequested);
        controller.handleIpcCommand(QStringLiteral("quit"), {}, nullptr);
        QCOMPARE(requested.count(), 1);
    }

#endif

#ifdef Q_OS_LINUX
    void setupAssistantPutsTheGlobalShortcutBeforeFinish()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        SetupAssistant assistant(&controller);

        const QStringList titles = assistant.pageTitles();
        QCOMPARE(titles,
                 QStringList({QStringLiteral("Welcome to Speecher"),
                              QStringLiteral("Transcription"),
                              QStringLiteral("Microphone"),
                              QStringLiteral("Desktop accessibility"),
                              QStringLiteral("Text delivery"),
                              QStringLiteral("Refinement"),
                              QStringLiteral("Writing profiles"),
                              QStringLiteral("Global Shortcut"),
                              QStringLiteral("Ready to dictate")}));
        QVERIFY(titles.indexOf(QStringLiteral("Global Shortcut"))
                < titles.indexOf(QStringLiteral("Ready to dictate")));

        WelcomeSetupPage *welcome = nullptr;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            if (auto *page = dynamic_cast<WelcomeSetupPage *>(widget)) {
                welcome = page;
                break;
            }
        }
        QVERIFY(welcome);
        bool mentionsShortcut = false;
        for (const QLabel *label : welcome->findChildren<QLabel *>()) {
            mentionsShortcut = mentionsShortcut
                || label->text().contains(QStringLiteral("ends by setting up a Global Shortcut"));
        }
        QVERIFY(mentionsShortcut);

        // The shortcut page is also a settings card control; as an assistant
        // page it keeps the same margins as the pages around it.
        LinuxGlobalShortcutSetupPage *shortcut = nullptr;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            if (auto *page = dynamic_cast<LinuxGlobalShortcutSetupPage *>(widget)) {
                shortcut = page;
                break;
            }
        }
        QVERIFY(shortcut);
        QCOMPARE(shortcut->layout()->contentsMargins(), welcome->layout()->contentsMargins());
        QCOMPARE(shortcut->layout()->contentsMargins().left(), setupPageMargin());

        // The assistant keeps the application palette rather than retuning a
        // role to fight its own style's separator.
        QVERIFY(!assistant.testAttribute(Qt::WA_SetPalette));
        QCOMPARE(assistant.palette().color(QPalette::Mid),
                 QApplication::palette().color(QPalette::Mid));
    }

    void setupAssistantHidesSkipOnTheLastPage()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        SetupAssistant assistant(&controller);
        assistant.show();
        QCoreApplication::processEvents();

        QAbstractButton *skip = nullptr;
        for (QAbstractButton *button : assistant.findChildren<QAbstractButton *>()) {
            if (button->text() == QStringLiteral("Skip setup")) {
                skip = button;
                break;
            }
        }
        QVERIFY(skip);
        QVERIFY(skip->isVisible());
        const int lastPage = assistant.pageTitles().indexOf(QStringLiteral("Ready to dictate"));
        QCOMPARE(lastPage, assistant.pageTitles().size() - 1);
#ifdef SPEECHER_WITH_KASSISTANT
        for (int step = 0; step < lastPage; ++step) {
            assistant.next();
        }
#else
        assistant.setCurrentId(assistant.pageIds().at(lastPage));
#endif
        QCoreApplication::processEvents();
        QVERIFY(!skip->isVisible());
    }

    void globalShortcutSinglePageOnlyShowsTheShortcutPage()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        SetupAssistant assistant(&controller, SetupAssistantPage::GlobalShortcut);
        assistant.show();

        QCOMPARE(assistant.pageTitles(), QStringList({QStringLiteral("Global Shortcut")}));
        int visibleSetupPages = 0;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            const bool setupPage = dynamic_cast<LinuxGlobalShortcutSetupPage *>(widget)
                || dynamic_cast<WelcomeSetupPage *>(widget)
                || dynamic_cast<MicrophoneSetupPage *>(widget);
            visibleSetupPages += setupPage && widget->isVisible();
        }
        QCOMPARE(visibleSetupPages, 1);
    }

    void globalShortcutPageEditsNativeShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const QKeySequence initial(Qt::META | Qt::ALT | Qt::Key_D);
        platform->binder->publishShortcut(initial);
        LinuxGlobalShortcutSetupPage page(controller);

        auto *sequence = page.findChild<QKeySequenceEdit *>();
        QVERIFY(sequence);
        QCOMPARE(sequence->keySequence(), initial);
        bool hasGuidance = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasGuidance = hasGuidance
                || label->text() == QStringLiteral(
                    "Press the keys you want to use for dictation.");
        }
        QVERIFY(hasGuidance);

        QPushButton *setShortcut = nullptr;
        for (QPushButton *button : page.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Set shortcut")) {
                setShortcut = button;
                break;
            }
        }
        QVERIFY(setShortcut);
        QVERIFY(!setShortcut->isEnabled());
        sequence->clear();
        QVERIFY(!setShortcut->isEnabled());

        page.show();
        sequence->setFocus();
        QTRY_VERIFY(sequence->hasFocus());
        const QKeySequence chosen(Qt::CTRL | Qt::ALT | Qt::Key_Space);
        sequence->setKeySequence(chosen);
        QVERIFY(setShortcut->isEnabled());
        platform->binder->publishShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        QCOMPARE(sequence->keySequence(), chosen);
        setShortcut->click();
        QCOMPARE(controller.globalShortcut(), chosen);
        QVERIFY(!setShortcut->isEnabled());

        bool hasStatus = false;
        for (QLabel *label : page.findChildren<QLabel *>()) {
            hasStatus = hasStatus
                || label->text() == QStringLiteral("Shortcut set to Ctrl+Alt+Space. Try it now.");
        }
        QVERIFY(hasStatus);

        platform->binder->setShortcutError = QStringLiteral("That shortcut is already in use.");
        sequence->setKeySequence(QKeySequence(Qt::CTRL | Qt::Key_D));
        setShortcut->click();
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"))->text(),
                 QStringLiteral("That shortcut is already in use."));
    }

    void globalShortcutPageWaitsForPortalSupportAndShowsItsResult()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutSupportKnown = false;
        platform->binder->shortcutsSupported = false;
        platform->binder->desktopChooser = true;
        LinuxGlobalShortcutSetupPage page(controller);

        auto *portal = page.findChild<QWidget *>(QStringLiteral("portalShortcut"));
        auto *status = page.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"));
        QVERIFY(portal);
        QVERIFY(!portal->isHidden());
        bool hasGuidance = false;
        for (const QLabel *label : portal->findChildren<QLabel *>()) {
            hasGuidance = hasGuidance
                || label->text() == QStringLiteral(
                    "Your desktop will ask you to pick a key combination.");
        }
        QVERIFY(hasGuidance);
        QVERIFY(status);
        QCOMPARE(status->text(), QStringLiteral("Checking your desktop…"));

        QPushButton *chooseShortcut = nullptr;
        for (QPushButton *button : page.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Choose shortcut")) {
                chooseShortcut = button;
                break;
            }
        }
        QVERIFY(chooseShortcut);
        QVERIFY(!chooseShortcut->isEnabled());

        platform->binder->publishSupport(true, true);
        QVERIFY(chooseShortcut->isEnabled());
        chooseShortcut->click();
        QCOMPARE(platform->binder->registerCount, 1);

        const QString result = QStringLiteral("Ctrl+Alt+Space");
        platform->binder->publishShortcut(QKeySequence(result));
        platform->binder->publishRegistrationResult(true, result);
        QCOMPARE(status->text(),
                 QStringLiteral("Shortcut set to Ctrl+Alt+Space. Try it now."));
    }

    void globalShortcutPageKeepsPortalFailureAfterRestoringTheOldShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->desktopChooser = true;
        const QKeySequence existing(Qt::CTRL | Qt::ALT | Qt::Key_Space);
        platform->binder->publishShortcut(existing);
        LinuxGlobalShortcutSetupPage page(controller);
        auto *status = page.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"));
        QVERIFY(status);

        platform->binder->publishRegistrationResult(
            false, QStringLiteral("Setup was cancelled. Try again."));
        platform->binder->publishShortcut(existing);

        QCOMPARE(status->text(), QStringLiteral("Setup was cancelled. Try again."));
    }

    void globalShortcutPageShowsOnlyManualSetupWhenUnsupported()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutsSupported = false;
        platform->binder->unsupported = QStringLiteral("Internal binder detail");
        LinuxGlobalShortcutSetupPage page(controller);

        auto *manual = page.findChild<QWidget *>(QStringLiteral("manualShortcut"));
        QVERIFY(manual);
        QVERIFY(!manual->isHidden());
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("keySequenceShortcut"))->isHidden());
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("portalShortcut"))->isHidden());
        QCOMPARE(page.findChildren<QGroupBox *>().size(), 0);

        bool hasInstruction = false;
        bool hasInternalDetail = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasInstruction = hasInstruction
                || label->text() == linuxGlobalShortcutManualInstruction();
            hasInternalDetail = hasInternalDetail
                || label->text() == QStringLiteral("Internal binder detail");
        }
        QVERIFY(hasInstruction);
        QVERIFY(!hasInternalDetail);
        auto *command = page.findChild<QLabel *>(QStringLiteral("globalShortcutCommand"));
        QVERIFY(command);
        QVERIFY(command->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QVERIFY(!command->text().isEmpty());
    }

    void finishPageNamesTheBoundGlobalShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->publishShortcut(
            QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        FinishSetupPage page(controller);
        page.show();
        QCoreApplication::processEvents();

        bool hasInstruction = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasInstruction = hasInstruction
                || label->text() == QStringLiteral(
                    "Press Meta+Alt+D to start dictating, speak, then press it again to stop and insert the text.");
        }
        QVERIFY(hasInstruction);
    }

    void finishPageShowsTheManualGlobalShortcutCommand()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutsSupported = false;
        FinishSetupPage page(controller);
        page.show();
        QCoreApplication::processEvents();

        bool hasInstruction = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasInstruction = hasInstruction
                || label->text() == linuxGlobalShortcutManualInstruction();
        }
        QVERIFY(hasInstruction);
        auto *command = page.findChild<QLabel *>(
            QStringLiteral("finishGlobalShortcutCommand"));
        QVERIFY(command);
        QCOMPARE(command->text(), linuxGlobalShortcutCommand());
        QVERIFY(!command->isHidden());
    }

    void finishPageExplainsWhenASupportedShortcutIsUnset()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutsSupported = true;
        FinishSetupPage page(controller);

        auto *status = page.findChild<QLabel *>(QStringLiteral("finishGlobalShortcutStatus"));
        auto *command = page.findChild<QLabel *>(
            QStringLiteral("finishGlobalShortcutCommand"));
        QVERIFY(status);
        QCOMPARE(status->text(), QStringLiteral(
            "No Global Shortcut is set yet. Go back to set one, or bind this command yourself:"));
        QVERIFY(command);
        QVERIFY(!command->isHidden());
    }

    void finishPageUpdatesWhenThePortalPublishesAShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FinishSetupPage page(controller);
        auto *status = page.findChild<QLabel *>(QStringLiteral("finishGlobalShortcutStatus"));
        auto *command = page.findChild<QLabel *>(
            QStringLiteral("finishGlobalShortcutCommand"));
        QVERIFY(status);
        QVERIFY(command);

        platform->binder->publishShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));

        QCOMPARE(status->text(), QStringLiteral(
            "Press Meta+Alt+D to start dictating, speak, then press it again to stop and insert the text."));
        QVERIFY(command->isHidden());
    }

    void globalShortcutInstructionCommandMatchesTheInstallation()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());

        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     QString(),
                     QStringLiteral("/opt/Speecher Current/bin/speecher")),
                 QStringLiteral("\"/opt/Speecher Current/bin/speecher\" toggle"));
        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     QStringLiteral("/opt/Speecher Current.AppImage"),
                     QStringLiteral("/tmp/.mount/usr/bin/speecher")),
                 QStringLiteral("\"/opt/Speecher Current.AppImage\" toggle"));

        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral(".local/bin"))));
        const QString appImage = home.filePath(QStringLiteral("Speecher.AppImage"));
        QFile source(appImage);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.close();
        const QString link = home.filePath(QStringLiteral(".local/bin/speecher"));
        const QString staleImage = home.filePath(QStringLiteral("Old Speecher.AppImage"));
        QFile stale(staleImage);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        QVERIFY(QFile::link(staleImage, link));
        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     appImage,
                     QStringLiteral("/tmp/.mount/usr/bin/speecher")),
                 QStringLiteral("\"%1\" toggle").arg(appImage));

        QVERIFY(QFile::remove(link));
        QVERIFY(QFile::link(appImage, link));
        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     appImage,
                     QStringLiteral("/tmp/.mount/usr/bin/speecher")),
                 QStringLiteral("\"%1\" toggle").arg(link));
    }

    void launchAtLoginWritesAndRemovesTheXdgAutostartEntry()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray oldHome = qgetenv("HOME");
        const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        const auto restoreEnvironment = qScopeGuard([oldHome, oldConfigHome] {
            oldHome.isNull() ? qunsetenv("HOME") : qputenv("HOME", oldHome);
            oldConfigHome.isNull() ? qunsetenv("XDG_CONFIG_HOME")
                                   : qputenv("XDG_CONFIG_HOME", oldConfigHome);
        });
        const QString home = root.filePath(QStringLiteral("home"));
        QVERIFY(QDir().mkpath(home));
        qputenv("HOME", QFile::encodeName(home));
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(root.filePath(QStringLiteral("xdg-config"))));
        const QString configHome = QStandardPaths::writableLocation(
            QStandardPaths::ConfigLocation);

        const QString executable = root.filePath(QStringLiteral("Speecher Current.AppImage"));
        QString error;
        QVERIFY2(setLaunchAtLoginAutostart(true, executable, &error), qPrintable(error));
        const QString entry = QDir(configHome).filePath(
            QStringLiteral("autostart/io.github.firemonster612.speecher.desktop"));
        QFile file(entry);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),
                 QByteArray("[Desktop Entry]\n"
                            "Type=Application\n"
                            "Name=Speecher\n"
                            "Icon=io.github.firemonster612.speecher\n"
                            "Exec=\"")
                     + QFile::encodeName(executable)
                     + QByteArray("\" --daemon\nHidden=false\n"));
        QVERIFY(launchAtLoginAutostartEnabled());

        QVERIFY2(setLaunchAtLoginAutostart(false, executable, &error), qPrintable(error));
        QVERIFY(!QFile::exists(entry));
        QVERIFY(!launchAtLoginAutostartEnabled());
    }

    void linuxCompositionReportsLaunchAtLoginState()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray oldHome = qgetenv("HOME");
        const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        const auto restoreEnvironment = qScopeGuard([oldHome, oldConfigHome] {
            oldHome.isNull() ? qunsetenv("HOME") : qputenv("HOME", oldHome);
            oldConfigHome.isNull() ? qunsetenv("XDG_CONFIG_HOME")
                                   : qputenv("XDG_CONFIG_HOME", oldConfigHome);
        });
        qputenv("HOME", QFile::encodeName(root.filePath(QStringLiteral("home"))));
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(root.filePath(QStringLiteral("xdg-config"))));

        LinuxComposition composition;
        QString error;
        QVERIFY2(composition.setLaunchAtLogin(true, &error), qPrintable(error));
        QVERIFY(composition.launchAtLoginEnabled());
        QVERIFY2(composition.setLaunchAtLogin(false, &error), qPrintable(error));
        QVERIFY(!composition.launchAtLoginEnabled());
    }

    void startupAdoptsAnExistingLaunchAtLoginEntry()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray oldHome = qgetenv("HOME");
        const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        const auto restoreEnvironment = qScopeGuard([oldHome, oldConfigHome] {
            oldHome.isNull() ? qunsetenv("HOME") : qputenv("HOME", oldHome);
            oldConfigHome.isNull() ? qunsetenv("XDG_CONFIG_HOME")
                                   : qputenv("XDG_CONFIG_HOME", oldConfigHome);
        });
        qputenv("HOME", QFile::encodeName(root.filePath(QStringLiteral("home"))));
        qunsetenv("XDG_CONFIG_HOME");
        const QString entry = QDir(QStandardPaths::writableLocation(
                                       QStandardPaths::ConfigLocation))
                                  .filePath(QStringLiteral(
                                      "autostart/io.github.firemonster612.speecher.desktop"));
        SettingsStore stored;
        stored.raw().clear();
        stored.raw().setValue(SettingsKeys::LaunchAtLogin, false);
        stored.raw().sync();
        QVERIFY(QDir().mkpath(QFileInfo(entry).dir().path()));
        QFile existing(entry);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QVERIFY(existing.write("[Desktop Entry]\nExec=speecher\n") > 0);
        existing.close();
        const auto cleanup = qScopeGuard([entry] {
            QFile::remove(entry);
            SettingsStore settings;
            settings.raw().clear();
        });

        ApplicationController controller(true, platformComposition());

        QVERIFY(QFile::exists(entry));
        QVERIFY(controller.settings()->launchAtLogin());
    }

    void appImageIntegrationRemovalUndoesTheInstallAndReportsIt()
    {
        const QByteArray oldAppImage = qgetenv("APPIMAGE");
        const QByteArray oldHome = qgetenv("HOME");
        const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
        const auto restoreEnvironment = qScopeGuard([oldAppImage, oldHome, oldConfigHome] {
            if (oldAppImage.isNull()) {
                qunsetenv("APPIMAGE");
            } else {
                qputenv("APPIMAGE", oldAppImage);
            }
            oldHome.isNull() ? qunsetenv("HOME") : qputenv("HOME", oldHome);
            oldConfigHome.isNull() ? qunsetenv("XDG_CONFIG_HOME")
                                   : qputenv("XDG_CONFIG_HOME", oldConfigHome);
        });
        qunsetenv("APPIMAGE");

        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QDir home(root.filePath(QStringLiteral("home")));
        QVERIFY(QDir().mkpath(home.path()));
        qputenv("HOME", QFile::encodeName(home.path()));
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(root.filePath(QStringLiteral("xdg-config"))));
        // The AppImage mount: usr/bin/speecher with the desktop file and icon
        // two levels up, as installAppImageIntegration expects.
        const QString appDir = root.filePath(QStringLiteral("mount"));
        const QString binDir = appDir + QStringLiteral("/usr/bin");
        QVERIFY(QDir().mkpath(binDir));
        const auto writeFile = [](const QString &path, const QByteArray &contents) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                return false;
            }
            return file.write(contents) == contents.size();
        };
        QVERIFY(writeFile(appDir + QStringLiteral("/io.github.firemonster612.speecher.desktop"),
                          "[Desktop Entry]\nExec=speecher\n"));
        QVERIFY(writeFile(appDir + QStringLiteral("/io.github.firemonster612.speecher.svg"),
                          "<svg/>"));
        const QString appImage = root.filePath(QStringLiteral("Speecher.AppImage"));
        QVERIFY(writeFile(appImage, "image"));

        QString error;
        QVERIFY2(installAppImageIntegration(home.path(), appImage, binDir, &error), qPrintable(error));
        const QString desktopFile = home.filePath(
            QStringLiteral(".local/share/applications/io.github.firemonster612.speecher.desktop"));
        const QString icon = home.filePath(
            QStringLiteral(".local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"));
        const QString link = home.filePath(QStringLiteral(".local/bin/speecher"));
        const QString helper = home.filePath(
            QStringLiteral(".local/share/speecher/libexec/speecher-ydotool-setup"));
        QVERIFY(QDir().mkpath(QFileInfo(helper).dir().path()));
        QVERIFY(writeFile(helper, "helper"));
        QVERIFY(QFile::exists(desktopFile));
        QVERIFY(QFile::exists(icon));
        QVERIFY(QFile::exists(helper));
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(setLaunchAtLoginAutostart(true, appImage, &error));

        DesktopIntegrationRemoval removal = removeAppImageIntegration(home.path());
        QCOMPARE(removal.removed,
                 QStringList({QStringLiteral("app menu entry"),
                              QStringLiteral("speecher command"),
                              QStringLiteral("app icon"),
                              QStringLiteral("local ydotool setup helper"),
                              QStringLiteral("launch at login entry")}));
        QVERIFY(removal.absent.isEmpty());
        QVERIFY(removal.failed.isEmpty());
        QVERIFY(!QFile::exists(desktopFile));
        QVERIFY(!QFile::exists(icon));
        QVERIFY(!QFile::exists(helper));
        QVERIFY(!QFileInfo(link).isSymLink() && !QFile::exists(link));
        // The program file is the user's to delete.
        QVERIFY(QFile::exists(appImage));

        // A second run finds nothing and says so rather than failing.
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.removed.isEmpty());
        QCOMPARE(removal.absent.size(), 5);
        QVERIFY(removal.failed.isEmpty());

        // A real file where the link belongs is not Speecher's to delete.
        QVERIFY(writeFile(link, "#!/bin/sh\n"));
        removal = removeAppImageIntegration(home.path());
        QCOMPARE(removal.failed.size(), 1);
        QVERIFY(removal.failed.first().startsWith(QStringLiteral("speecher command")));
        QVERIFY(QFile::exists(link));

        QVERIFY(QFile::remove(link));
        const QString renamed = root.filePath(QStringLiteral("dictation"));
        QVERIFY(writeFile(renamed, "image"));
        QVERIFY(QFile::link(renamed, link));
        qputenv("APPIMAGE", QFile::encodeName(renamed));
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.removed.contains(QStringLiteral("speecher command")));
        QVERIFY(!QFileInfo(link).isSymLink());

        qunsetenv("APPIMAGE");
        const QString unrelated = home.filePath(QStringLiteral("SomeoneElse.AppImage"));
        QVERIFY(writeFile(unrelated, "image"));
        QVERIFY(QFile::link(unrelated, link));
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.failed.contains(QStringLiteral(
            "speecher command: the link does not point to a Speecher AppImage")));
        QVERIFY(QFileInfo(link).isSymLink());
    }

    void appImageDesktopFileExecLinesUseTheRealImagePath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath = directory.filePath(QStringLiteral("source.desktop"));
        const QString targetPath = directory.filePath(QStringLiteral("installed.desktop"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("[Desktop Entry]\nExec=speecher\n"
                     "[Desktop Action ToggleDictation]\nExec=speecher toggle\n"
                     "[Desktop Action Quoted]\nExec=\"/old Speecher.AppImage\" toggle\n");
        source.close();

        QString error;
        QVERIFY2(writeAppImageDesktopFile(sourcePath,
                                          targetPath,
                                          QStringLiteral("/opt/Speecher Current.AppImage"),
                                          &error),
                 qPrintable(error));
        QFile installed(targetPath);
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(),
                 QByteArray("[Desktop Entry]\n"
                            "Exec=\"/opt/Speecher Current.AppImage\"\n"
                            "[Desktop Action ToggleDictation]\n"
                            "Exec=\"/opt/Speecher Current.AppImage\" toggle\n"
                            "[Desktop Action Quoted]\n"
                            "Exec=\"/opt/Speecher Current.AppImage\" toggle\n"));
    }

    void appImageIntegrationReplacesAStaleCommandLink()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString home = directory.filePath(QStringLiteral("home"));
        const QString appDir = directory.filePath(QStringLiteral("AppDir"));
        const QString binaryDir = QDir(appDir).filePath(QStringLiteral("usr/bin"));
        QVERIFY(QDir().mkpath(binaryDir));

        const QString desktop = QDir(appDir).filePath(
            QStringLiteral("io.github.firemonster612.speecher.desktop"));
        QFile desktopFile(desktop);
        QVERIFY(desktopFile.open(QIODevice::WriteOnly));
        desktopFile.write("[Desktop Entry]\nExec=speecher\n");
        desktopFile.close();

        const QString icon = QDir(appDir).filePath(
            QStringLiteral("io.github.firemonster612.speecher.svg"));
        QFile iconFile(icon);
        QVERIFY(iconFile.open(QIODevice::WriteOnly));
        iconFile.write("<svg/>\n");
        iconFile.close();

        const QString oldImage = directory.filePath(QStringLiteral("old.AppImage"));
        const QString newImage = directory.filePath(QStringLiteral("new.AppImage"));
        for (const QString &path : {oldImage, newImage}) {
            QFile image(path);
            QVERIFY(image.open(QIODevice::WriteOnly));
        }

        const QString commandDir = QDir(home).filePath(QStringLiteral(".local/bin"));
        QVERIFY(QDir().mkpath(commandDir));
        const QString command = QDir(commandDir).filePath(QStringLiteral("speecher"));
        QVERIFY(QFile::link(oldImage, command));

        QString error;
        QVERIFY2(installAppImageIntegration(home, newImage, binaryDir, &error),
                 qPrintable(error));
        QCOMPARE(resolvedPath(QFileInfo(command).symLinkTarget()), resolvedPath(newImage));
        QVERIFY(QFileInfo::exists(QDir(home).filePath(
            QStringLiteral(".local/share/applications/io.github.firemonster612.speecher.desktop"))));
        QVERIFY(QFileInfo::exists(QDir(home).filePath(
            QStringLiteral(".local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"))));
    }
#endif

    void correctionTrackerSettlesSamplesWithoutRealTimeWaits()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("I use cute every day");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");

        QList<CorrectionEvidence> observed;
        tracker.begin(window, [&observed](const QString &original,
                                          const QString &corrected,
                                          const QString &,
                                          double confidence) {
            observed.append({original, corrected, confidence});
        });
        tracker.sample(QStringLiteral("before text I use cute every day after text"));
        tracker.sample(QStringLiteral("before text I use Qt every day after text"));
        QCOMPARE(observed.size(), 0);
        tracker.sample(QStringLiteral("before text I use Qt every day after text"));

        QCOMPARE(observed.size(), 1);
        QCOMPARE(observed.first().original, QStringLiteral("cute"));
        QCOMPARE(observed.first().corrected, QStringLiteral("Qt"));
        QVERIFY(!tracker.active());
    }

    void correctionTrackerCancelsUnsettledOrUnreadableSamples()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.cancel();
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral(
            "before text Qt after text before text duplicate after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        window.target.selectionStart = 0;
        window.target.selectionEnd = 4;
        window.target.selectedText = QStringLiteral("cute");
        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);
    }

    void correctionTrackerIgnoresEditsThatAreNotCorrections()
    {
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        const auto observed = [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        };

        CorrectionTracker untouched;
        untouched.begin(window, observed);
        untouched.sample(QStringLiteral("before text cute after text"));
        untouched.sample(QStringLiteral("before text cute after text"));
        QCOMPARE(observations, 0);
        QVERIFY(untouched.active());

        CorrectionTracker punctuated;
        punctuated.begin(window, observed);
        punctuated.sample(QStringLiteral("before text cute! after text"));
        punctuated.sample(QStringLiteral("before text cute! after text"));
        QCOMPARE(observations, 0);

        CorrectionTracker rewritten;
        rewritten.begin(window, observed);
        rewritten.sample(QStringLiteral("before text an entirely different phrase after text"));
        rewritten.sample(QStringLiteral("before text an entirely different phrase after text"));
        QCOMPARE(observations, 0);
    }

    void correctionTrackerDisablePreventsAndCancelsObservation()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        const auto observed = [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        };

        tracker.setEnabled(false);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.setEnabled(true);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.setEnabled(false);
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.setEnabled(true);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 1);
    }

    void controllerAnswersFromTheInjectedComposition()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);

        QCOMPARE(controller.platform(), platform.get());
        QCOMPARE(controller.outputSummary(), QStringLiteral("Fake: nothing is delivered"));
    }

    void shortcutApiDelegatesToTheCompositionsBinder()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);

        QVERIFY(controller.globalShortcutsSupported());
        QVERIFY(controller.globalShortcut().isEmpty());

        QString error;
        QVERIFY(!controller.setGlobalShortcut(QKeySequence(), &error));
        QCOMPARE(error, QStringLiteral("fake binder rejects an empty sequence"));

        const QKeySequence chosen(Qt::META | Qt::ALT | Qt::Key_D);
        QVERIFY(controller.setGlobalShortcut(chosen));
        QCOMPARE(controller.globalShortcut(), chosen);
    }

    void deferredStartupBindsTheShortcutAndPublishesAccessibility()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QSignalSpy accessibilityChanged(&controller,
                                        &ApplicationController::accessibilityStateChanged);
        QVERIFY(platform->binder);
        QCOMPARE(platform->binder->bindCount, 0);

        controller.frontEndReady();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);

        QCOMPARE(platform->binder->bindCount, 1);
        QVERIFY(controller.accessibilitySupported());
        QVERIFY(controller.accessibilityEnabled());
        QVERIFY(!controller.accessibilityPersistent());

        // The fallback timer must not run the startup a second time.
        controller.frontEndReady();
        QTest::qWait(20);
        QCOMPARE(platform->binder->bindCount, 1);
    }

    void accessibilityChangesRefreshTheControllersCachedState()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->accessibility = {true, false, false};
        ApplicationController controller(true, platform);
        QSignalSpy accessibilityChanged(&controller,
                                        &ApplicationController::accessibilityStateChanged);

        controller.frontEndReady();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);
        QVERIFY(!controller.accessibilityEnabled());
        QVERIFY(platform->accessibilityRefresh);

        platform->accessibility = {true, true, true};
        platform->accessibilityRefresh();

        QVERIFY(controller.accessibilityEnabled());
        QVERIFY(controller.accessibilityPersistent());
        QCOMPARE(accessibilityChanged.count(), 2);
        QCOMPARE(accessibilityChanged.last().at(1).toBool(), true);
    }

    void windowRequestsGoToTheFrontEnd()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->setSetupCompleted(true);

        controller.showMain();
        controller.showSettings();
        controller.showSetup();
        QVERIFY(controller.grabMainWindow(QStringLiteral("/tmp/speecher-grab.png")));

        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("showMainWindow"),
                              QStringLiteral("showSettingsWindow"),
                              QStringLiteral("showSetupAssistant"),
                              QStringLiteral("captureMainWindow /tmp/speecher-grab.png")}));
    }

    void unfinishedSetupSendsTheUserToTheAssistantInstead()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->setSetupCompleted(false);

        controller.showMain();
        controller.showSettings();

        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("showSetupAssistant"),
                              QStringLiteral("showSetupAssistant")}));
    }
};

int runPlatformCompositionTests(int argc, char **argv)
{
    PlatformCompositionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_platform_composition.moc"
