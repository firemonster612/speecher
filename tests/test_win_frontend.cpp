#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "frontend/win/CustomRows.h"
#include "frontend/win/DictationPanel.h"
#include "frontend/win/SettingsWindow.h"
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
        const QList<RowOption> methods = win::customRowOptions(
            QStringLiteral("outputMethod"), controller->settings()->snapshot(),
            *controller->settings());
        for (const RowOption &method : methods) {
            if (method.id == QString::fromLatin1(OutputMethod::WinPaste)) {
                QCOMPARE(method.label, QStringLiteral("Keyboard paste (Ctrl+V)"));
                return;
            }
        }
        QFAIL("Windows paste is missing from the output methods");
    }

    void authOptionLabelsAreReconciledWithW2()
    {
        const AppSettings draft = controller->settings()->snapshot();
        const auto labels = [](const QList<RowOption> &options) {
            QStringList result;
            for (const RowOption &option : options) {
                result.append(option.label);
            }
            return result;
        };
        QCOMPARE(labels(win::customRowOptions(QStringLiteral("openAiAuthMode"), draft,
                                              *controller->settings())),
                 QStringList({QStringLiteral("Automatic"),
                              QStringLiteral("API key from the Codex app"),
                              QStringLiteral("ChatGPT sign-in from the Codex app"),
                              QStringLiteral("API key from the environment"),
                              QStringLiteral("API key saved in Speecher"),
                              QStringLiteral("CLI Proxy API account")}));
        QCOMPARE(labels(win::customRowOptions(QStringLiteral("anthropicAuthMode"), draft,
                                              *controller->settings())),
                 QStringList({QStringLiteral("Claude Code sign-in"),
                              QStringLiteral("CLI Proxy API account")}));
    }

    void whatsNewOfferIsReconciledWithW2()
    {
        QVERIFY(win::SettingsWindow::offersWhatsNew(QStringLiteral("general"),
                                                    QStringLiteral("0.1.0")));
        QVERIFY(win::SettingsWindow::offersWhatsNew(QStringLiteral("whatsNew"), {}));
        QVERIFY(!win::SettingsWindow::offersWhatsNew(QStringLiteral("general"), {}));
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
