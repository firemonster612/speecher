#pragma once

#include <QByteArray>
#include <QMetaMethod>
#include <QTest>
#include <QVector>

#include <cstdio>

inline QVector<QByteArray> &testArguments()
{
    static QVector<QByteArray> arguments;
    return arguments;
}

inline void setTestArguments(int argc, char **argv)
{
    QVector<QByteArray> &arguments = testArguments();
    arguments.clear();
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        arguments.append(argv[index]);
    }
}

inline int runTestSuite(QObject *suite, int argc, char **argv)
{
    Q_UNUSED(argc);
    Q_UNUSED(argv);
    QVector<QByteArray> arguments = testArguments();
    if (arguments.contains(QByteArrayLiteral("-functions"))) {
        const QMetaObject *metaObject = suite->metaObject();
        for (int index = metaObject->methodOffset(); index < metaObject->methodCount(); ++index) {
            const QMetaMethod method = metaObject->method(index);
            if (method.methodType() == QMetaMethod::Slot
                && method.name() != QByteArrayLiteral("initTestCase")
                && method.name() != QByteArrayLiteral("cleanupTestCase")) {
                std::puts(method.methodSignature().constData());
            }
        }
        return 0;
    }
    QVector<char *> copiedArguments;
    copiedArguments.reserve(arguments.size());
    for (QByteArray &argument : arguments) {
        copiedArguments.append(argument.data());
    }
    return QTest::qExec(suite, copiedArguments.size(), copiedArguments.data());
}

int runTranscriptStateTests(int argc, char **argv);
int runBindingsTests(int argc, char **argv);
int runSettingsTests(int argc, char **argv);
int runProviderRegistryTests(int argc, char **argv);
int runPlatformCompositionTests(int argc, char **argv);
int runTrayTests(int argc, char **argv);
#ifdef Q_OS_LINUX
int runKeywatchTests(int argc, char **argv);
int runHelperInstallTests(int argc, char **argv);
int runQuickSettingsTests(int argc, char **argv);
int runQuickAppearanceTests(int argc, char **argv);
int runQuickDictationTests(int argc, char **argv);
int runQuickUpdatesTests(int argc, char **argv);
int runQuickTranscriptionTests(int argc, char **argv);
int runQuickSystemTests(int argc, char **argv);
int runQuickModelsTests(int argc, char **argv);
int runQuickInsightsTests(int argc, char **argv);
int runQuickAccountsTests(int argc, char **argv);
int runQuickAudioTests(int argc, char **argv);
int runQuickLibraryTests(int argc, char **argv);
#endif
int runPlatformLiveTests(int argc, char **argv);
int runSingleInstanceIpcTests(int argc, char **argv);
int runDeliveryTests(int argc, char **argv);
int runDictationSessionLifecycleTests(int argc, char **argv);
int runDictationSessionRefinementTests(int argc, char **argv);
int runDictationSessionFallbacksTests(int argc, char **argv);
int runRefinersTests(int argc, char **argv);
int runVocabularyTests(int argc, char **argv);
int runPhoneTransferTests(int argc, char **argv);
int runInsightsTests(int argc, char **argv);
int runProviderAuthTests(int argc, char **argv);
int runClaudeVoiceTests(int argc, char **argv);
int runCodexDictationTests(int argc, char **argv);
int runAudioPcmConverterTests(int argc, char **argv);
int runEchoCancellerTests(int argc, char **argv);
int runLocalModelsTests(int argc, char **argv);
int runCustomEndpointsTests(int argc, char **argv);
int runFileTranscriptionTests(int argc, char **argv);
#ifdef Q_OS_UNIX
int runUpdateControllerTests(int argc, char **argv);
#endif
#ifdef Q_OS_MACOS
int runMacPlatformTests(int argc, char **argv);
#endif
#ifdef Q_OS_WIN
int runWinPlatformTests(int argc, char **argv);
#endif
