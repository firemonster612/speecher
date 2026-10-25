#include "common/test_suites.h"
#include "app/ProviderSetup.h"
#include "providers/ProviderRegistry.h"
#include "common/test_doubles.h"

using namespace speecher;
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

    // The bars replace the Score and the text Speed, Accuracy and Quality
    // lines; engines and default models moved into Advanced; and the Speed
    // line left on speech is Text shows, as on the Local models page.
    void providerStatsLeaveRatingsToTheBars()
    {
        ProviderRegistry registry;
        registerProviders(registry, nullptr, nullptr);
        const QStringList moved{QStringLiteral("Score"), QStringLiteral("Speed"), QStringLiteral("Accuracy"),
                                QStringLiteral("Quality"), QStringLiteral("Engine"), QStringLiteral("Default model")};
        QList<ProviderDescriptor> providers = registry.speechProviders() + registry.refinementProviders();
        QHash<QString, ProviderDescriptor> speech;
        for (const ProviderDescriptor &provider : registry.speechProviders()) {
            speech.insert(provider.id, provider);
        }
        for (const ProviderDescriptor &provider : providers) {
            for (const ProviderStat &stat : provider.stats) {
                QVERIFY2(!moved.contains(stat.label), qPrintable(provider.id + QLatin1Char('/') + stat.label));
            }
        }
        const auto labels = [](const ProviderDescriptor &provider) {
            QStringList names;
            for (const ProviderStat &stat : provider.stats) {
                names.append(stat.label);
            }
            return names;
        };
        const QStringList cloudLabels{QStringLiteral("Languages"), QStringLiteral("Text shows"),
                                      QStringLiteral("Formatting")};
        QCOMPARE(labels(speech.value(QStringLiteral("codex"))), cloudLabels);
        QCOMPARE(labels(speech.value(QStringLiteral("claude"))), cloudLabels);
        // The Service row's help, which no longer names the engine.
        QCOMPARE(speech.value(QStringLiteral("codex")).summary, QStringLiteral("Very accurate, around 100 languages."));
        QCOMPARE(speech.value(QStringLiteral("claude")).summary,
                 QStringLiteral("About 60 languages, automatic punctuation and numerals."));
    }
};

int runProviderRegistryTests(int argc, char **argv)
{
    ProviderRegistryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_provider_registry.moc"
