#include "common/test_suites.h"
#include "app/ProviderSetup.h"
#include "core/settings/SettingsSchema.h"
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

    // Settings names a provider's card after it, as the registry does.
    void providerCardsCarryTheRegistryLabels()
    {
        ProviderRegistry registry;
        registerProviders(registry, nullptr, nullptr);
        const SettingsSchema schema = buildSettingsSchema({});
        const auto cardTitles = [&schema](const QString &pane) {
            QStringList titles;
            for (const SettingsPaneGroup &group : schema.pane(pane)->groups) {
                titles.append(group.title);
            }
            return titles;
        };
        const QStringList withCards{QStringLiteral("local"), QStringLiteral("endpoint")};
        for (const ProviderDescriptor &provider : registry.speechProviders()) {
            if (withCards.contains(provider.id)) {
                QVERIFY2(cardTitles(QStringLiteral("dictation")).contains(provider.label), qPrintable(provider.label));
            }
        }
        for (const ProviderDescriptor &provider : registry.refinementProviders()) {
            QVERIFY2(cardTitles(QStringLiteral("refinement")).contains(provider.label), qPrintable(provider.label));
        }
    }
};

int runProviderRegistryTests(int argc, char **argv)
{
    ProviderRegistryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_provider_registry.moc"
