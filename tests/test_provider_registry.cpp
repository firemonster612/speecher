#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;


class ProviderRegistryTests : public QObject {
    Q_OBJECT

private slots:
    void providerRegistryReturnsSingletonAdapters()
    {
        ProviderRegistry registry;
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(registry, &speech);
        registerFakeRefiner(registry, &refiner);

        QCOMPARE(registry.speechProviders().size(), 1);
        QCOMPARE(registry.refinementProviders().size(), 1);
        QVERIFY(registry.speechProvider(QStringLiteral("missing")) == nullptr);
        QVERIFY(registry.refinementProvider(QStringLiteral("missing")) == nullptr);
        SpeechTranscriber *speechProvider = registry.speechProvider(QStringLiteral("claude"));
        TranscriptRefiner *refinementProvider = registry.refinementProvider(QStringLiteral("openai"));
        QVERIFY(speechProvider);
        QVERIFY(refinementProvider);
        QCOMPARE(registry.speechProvider(QStringLiteral("claude")), speechProvider);
        QCOMPARE(registry.refinementProvider(QStringLiteral("openai")), refinementProvider);
        QCOMPARE(speechProvider, speech);
        QCOMPARE(refinementProvider, refiner);
    }
};

int runProviderRegistryTests(int argc, char **argv)
{
    ProviderRegistryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_provider_registry.moc"
