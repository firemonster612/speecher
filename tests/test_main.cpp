#include "common/test_suites.h"

#include <QApplication>
#include <QStandardPaths>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    setTestArguments(argc, argv);

    int result = 0;
    result |= runUiTests(argc, argv);
    result |= runTranscriptStateTests(argc, argv);
    result |= runBindingsTests(argc, argv);
    result |= runSettingsTests(argc, argv);
    result |= runProviderRegistryTests(argc, argv);
    result |= runPlatformLiveTests(argc, argv);
    result |= runSingleInstanceIpcTests(argc, argv);
    result |= runDeliveryTests(argc, argv);
    result |= runDictationSessionLifecycleTests(argc, argv);
    result |= runDictationSessionRefinementTests(argc, argv);
    result |= runRefinersTests(argc, argv);
    result |= runVocabularyTests(argc, argv);
    result |= runProviderAuthTests(argc, argv);
    result |= runClaudeVoiceTests(argc, argv);
    result |= runAudioPcmConverterTests(argc, argv);
    return result;
}
