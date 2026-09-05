#include "common/test_suites.h"

#include <QApplication>
#include <QStandardPaths>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
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
    return result;
}
