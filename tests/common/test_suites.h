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

int runUiTests(int argc, char **argv);
int runAppWindowTests(int argc, char **argv);
int runTranscriptStateTests(int argc, char **argv);
int runBindingsTests(int argc, char **argv);
int runSettingsTests(int argc, char **argv);
int runProviderRegistryTests(int argc, char **argv);
int runPlatformLiveTests(int argc, char **argv);
int runSingleInstanceIpcTests(int argc, char **argv);
int runDeliveryTests(int argc, char **argv);
int runDictationSessionLifecycleTests(int argc, char **argv);
int runDictationSessionRefinementTests(int argc, char **argv);
int runRefinersTests(int argc, char **argv);
int runVocabularyTests(int argc, char **argv);
int runProviderAuthTests(int argc, char **argv);
int runClaudeVoiceTests(int argc, char **argv);
int runCodexDictationTests(int argc, char **argv);
int runAudioPcmConverterTests(int argc, char **argv);
