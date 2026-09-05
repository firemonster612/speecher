#include "common/test_suites.h"

#include <QApplication>
#include <QDebug>
#include <QScopeGuard>
#include <QStandardPaths>

#ifdef SPEECHER_WITH_WINUI
#include "frontend/win/WinUiHost.h"
#include <winrt/base.h>
#endif

int main(int argc, char **argv)
{
#ifdef SPEECHER_WITH_WINUI
    std::unique_ptr<speecher::WinUiHost> winUiHost;
    try {
        winUiHost = std::make_unique<speecher::WinUiHost>();
    } catch (const winrt::hresult_error &error) {
        qWarning() << "Skipping WinUI tests:" << QString::fromWCharArray(error.message().c_str());
    }
#endif
    QApplication app(argc, argv);
#ifdef SPEECHER_WITH_WINUI
    if (winUiHost) {
        winUiHost->installNativeEventFilter();
    }
    const auto winUiShutdown = qScopeGuard([&winUiHost] {
        if (winUiHost) {
            winUiHost->shutdown();
        }
    });
#endif
    QStandardPaths::setTestModeEnabled(true);
    setTestArguments(argc, argv);

    if (qEnvironmentVariable("SPEECHER_TEST_ONLY_PLATFORM_LIVE") == QStringLiteral("1")) {
        return runPlatformLiveTests(argc, argv);
    }
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("SPEECHER_TEST_ONLY_WIN_PLATFORM") == QStringLiteral("1")) {
        return runWinPlatformTests(argc, argv);
    }
#endif
#ifdef SPEECHER_WITH_WINUI
    if (qEnvironmentVariable("SPEECHER_TEST_ONLY_WIN_FRONTEND") == QStringLiteral("1")) {
        if (!winUiHost) {
            return 0;
        }
        return runWinFrontEndTests(argc, argv, std::move(winUiHost));
    }
#endif

    int result = 0;
    result |= runUiTests(argc, argv);
    result |= runAppWindowTests(argc, argv);
    result |= runTranscriptStateTests(argc, argv);
    result |= runBindingsTests(argc, argv);
    result |= runSettingsTests(argc, argv);
    result |= runSettingsSchemaTests(argc, argv);
    result |= runProviderRegistryTests(argc, argv);
    result |= runPlatformCompositionTests(argc, argv);
#ifdef Q_OS_LINUX
    result |= runLinuxStyleChoiceTests(argc, argv);
#endif
    result |= runPlatformLiveTests(argc, argv);
    result |= runSingleInstanceIpcTests(argc, argv);
    result |= runDeliveryTests(argc, argv);
    result |= runDictationSessionLifecycleTests(argc, argv);
    result |= runDictationSessionRefinementTests(argc, argv);
    result |= runRefinersTests(argc, argv);
    result |= runVocabularyTests(argc, argv);
    result |= runProviderAuthTests(argc, argv);
    result |= runClaudeVoiceTests(argc, argv);
    result |= runCodexDictationTests(argc, argv);
    result |= runAudioPcmConverterTests(argc, argv);
#ifdef Q_OS_UNIX
    result |= runUpdateControllerTests(argc, argv);
#endif
#ifdef SPEECHER_WITH_SWIFT_UI
    result |= runMacFrontEndTests(argc, argv);
#endif
#ifdef Q_OS_WIN
    result |= runWinPlatformTests(argc, argv);
#endif
#ifdef SPEECHER_WITH_WINUI
    if (winUiHost) {
        result |= runWinFrontEndTests(argc, argv, std::move(winUiHost));
    }
#endif
    return result;
}
