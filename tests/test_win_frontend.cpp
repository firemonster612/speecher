#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "frontend/win/DictationPanel.h"
#include "frontend/win/SetupWindow.h"
#include "frontend/win/WinFrontEnd.h"
#include "frontend/win/WinUiHost.h"
#include "ui/TranscriberPopup.h"

#include <windows.h>

#include <QApplication>
#include <QTest>

#include <memory>

namespace speecher {
namespace {

bool nativeUiAvailable()
{
    return QGuiApplication::platformName() != QStringLiteral("offscreen");
}

template<typename Widget>
int widgetCount()
{
    int count = 0;
    for (QWidget *widget : QApplication::allWidgets()) {
        count += dynamic_cast<Widget *>(widget) != nullptr;
    }
    return count;
}

} // namespace

class WinFrontEndTests : public QObject {
    Q_OBJECT

public:
    explicit WinFrontEndTests(std::unique_ptr<WinUiHost> host)
        : host(std::move(host))
    {
    }

private slots:
    void initTestCase()
    {
        SettingsStore settings;
        settings.raw().clear();
        existingQtPopups = widgetCount<TranscriberPopup>();
        controller = std::make_unique<ApplicationController>(false);
        frontEnd = std::make_unique<WinFrontEnd>(controller.get(), std::move(host));
        controller->setFrontEnd(frontEnd.get());
        setup = std::make_unique<SetupWindow>(controller.get(), [] {});
    }

    void cleanupTestCase()
    {
        setup.reset();
        frontEnd.reset();
        controller.reset();
    }

    void constructionDoesNotCreateAQtDictationPopup()
    {
        QCOMPARE(widgetCount<TranscriberPopup>(), existingQtPopups);
    }

    void nativeDictationProblemCanBeDismissed()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        frontEnd->showDictationError(QStringLiteral("The microphone stopped"));
        QVERIFY(frontEnd->panelVisibleForTest());

        frontEnd->dismissPanelForTest();
        QVERIFY(!frontEnd->panelVisibleForTest());
    }

    void nativeDictationPanelUsesNonActivatingTopmostToolWindowStyles()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        frontEnd->showDictationError(QStringLiteral("Style probe"));
        const qintptr style = frontEnd->panelWindowStyleForTest();
        QVERIFY(style & WS_EX_NOACTIVATE);
        QVERIFY(style & WS_EX_TOOLWINDOW);
        QVERIFY(style & WS_EX_TOPMOST);
        frontEnd->dismissPanelForTest();
    }

    void popupPresentationAcknowledgesRequestedGeneration()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI islands require an interactive desktop");
        }
        constexpr quint64 generation = 73;
        frontEnd->showPanelForTest(generation);
        QTRY_COMPARE_WITH_TIMEOUT(frontEnd->panelPresentedGenerationForTest(), generation, 2000);
        frontEnd->dismissPanelForTest();
    }

    void skippingSetupOpensTheNativeSettingsWindow()
    {
        if (!nativeUiAvailable()) {
            QSKIP("WinUI windows require an interactive desktop");
        }
        setup->show(SetupAssistantPage::All);
        setup->skipForTest();

        QTRY_VERIFY_WITH_TIMEOUT(FindWindowW(nullptr, L"Speecher") != nullptr, 2000);
        QVERIFY(controller->settings()->setupCompleted());
    }

    void capabilitiesFollowWindowsAccessibility()
    {
        controller->frontEndReady();
        QTRY_VERIFY_WITH_TIMEOUT(controller->accessibilitySupported(), 2000);
        QVERIFY(controller->accessibilityEnabled());
        QVERIFY(controller->accessibilityPersistent());
    }

    void outputMethodsIncludeWindowsPaste()
    {
        const QStringList methods = OutputMethod::all();
        QVERIFY(methods.contains(QStringLiteral("direct_insert")));
        if (!methods.contains(QStringLiteral("win-paste"))) {
            QSKIP("win-paste is supplied by W1 and asserted here after W5 merges it");
        }
        QVERIFY(methods.contains(QStringLiteral("win-paste")));
    }

    void authOptionLabelsAreReconciledWithW2()
    {
        QSKIP("the auth option model belongs to W2 CustomRows");
    }

    void whatsNewOfferIsReconciledWithW2()
    {
        QSKIP("the What's New offer belongs to W2 SettingsWindow");
    }

    void setupPageOrderAndCopyMatchTheWindowsFlow()
    {
        QCOMPARE(SetupWindow::pageTitles(),
                 QStringList({QStringLiteral("Welcome to Speecher"),
                              QStringLiteral("Transcription"),
                              QStringLiteral("Microphone"),
                              QStringLiteral("Text delivery"),
                              QStringLiteral("Refinement"),
                              QStringLiteral("Writing profiles"),
                              QStringLiteral("Global Shortcut"),
                              QStringLiteral("Start at login"),
                              QStringLiteral("Ready to dictate")}));
        QCOMPARE(SetupWindow::welcomeCopyForTest(),
                 QStringList({QStringLiteral("Speecher records a short dictation, turns it into text, and sends it to the app you were using."),
                              QStringLiteral("This assistant checks your transcription provider, microphone, desktop accessibility, text delivery, refinement, and writing profiles.")}));
        if (nativeUiAvailable()) {
            setup->show(SetupAssistantPage::GlobalShortcut);
            QCOMPARE(setup->currentPageTitleForTest(), QStringLiteral("Global Shortcut"));
        }
    }

    void panelVisualState()
    {
        const QString phase = qEnvironmentVariable("SPEECHER_WIN_PANEL_PHASE");
        if (phase.isEmpty()) {
            QSKIP("visual screenshot driver");
        }

        DictationSession *session = controller->session();
        if (phase == QStringLiteral("problem")) {
            frontEnd->showDictationError(QStringLiteral(
                "Microphone access is off. Check Windows privacy settings."));
        } else {
            frontEnd->showPanelForTest(91);
            session->popupStatusChanged(phase);
            session->previewDisplayChanged(
                QStringLiteral("A preview that grows while the user keeps dictating"));
            session->audioLevelChanged(0.65f);
            session->popupRefiningChanged(phase == QStringLiteral("Refining"));
        }
        QTest::qWait(15000);
        frontEnd->dismissPanelForTest();
    }

private:
    int existingQtPopups = 0;
    std::unique_ptr<WinUiHost> host;
    std::unique_ptr<ApplicationController> controller;
    std::unique_ptr<WinFrontEnd> frontEnd;
    std::unique_ptr<SetupWindow> setup;
};

} // namespace speecher

int runWinFrontEndTests(int argc, char **argv,
                        std::unique_ptr<speecher::WinUiHost> host)
{
    speecher::WinFrontEndTests tests(std::move(host));
    return runTestSuite(&tests, argc, argv);
}

#include "test_win_frontend.moc"
