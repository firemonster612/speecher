#include "common/test_suites.h"
#include "app/ProviderSetup.h"
#include "core/CleanupModelCatalog.h"
#include "core/ProviderRatings.h"
#include "providers/ProviderRegistry.h"
#include "common/test_doubles.h"

using namespace speecher;
using namespace speecher::test;

namespace {

HardwareProfile referencePc()
{
    HardwareProfile pc;
    pc.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
    pc.chipName = QStringLiteral("AMD Ryzen 7 PRO 4750U with Radeon Graphics");
    pc.systemRamBytes = quint64(16) << 30;
    pc.availableRamBytes = quint64(12) << 30;
    return pc;
}

HardwareProfile processorOnly()
{
    HardwareProfile cpu = referencePc();
    cpu.accelerator = HardwareProfile::Accelerator::Cpu;
    return cpu;
}

HardwareProfile graphicsCard(quint64 gigabytes)
{
    HardwareProfile card = referencePc();
    card.accelerator = HardwareProfile::Accelerator::DedicatedGpu;
    card.gpuMemoryBytes = gigabytes << 30;
    return card;
}

// The bars' values in order, -1 for a bar with no figure; empty without a
// rating.
QList<double> ratingValues(ProviderRole role, const QString &id, const HardwareProfile &hardware = {},
                           const AppSettings &settings = {})
{
    const std::optional<ProviderRating> rating = providerRating(role, id, hardware, settings);
    if (!rating) {
        return {};
    }
    QList<double> values;
    for (const Rating &bar : rating->bars) {
        values.append(bar.value.value_or(-1));
    }
    return values;
}

QString ratedModel(ProviderRole role, const QString &id, const HardwareProfile &hardware,
                   const AppSettings &settings = {})
{
    return providerRating(role, id, hardware, settings).value_or(ProviderRating{}).ratedModel;
}

} // namespace

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

    // Each measure's rule rounds to the nearest half and stays within 0..10.
    void ratingRulesRoundToHalvesWithinTen()
    {
        QCOMPARE(accuracyRating(2.0), 10.0);
        QCOMPARE(accuracyRating(0.8), 10.0);
        QCOMPARE(accuracyRating(5.0), 7.0);
        QCOMPARE(accuracyRating(4.8), 7.0);
        QCOMPARE(accuracyRating(4.7), 7.5);
        QCOMPARE(accuracyRating(13.5), 0.0);
        QCOMPARE(qualityRating(123, 123), 10.0);
        QCOMPARE(qualityRating(0, 123), 0.0);
        QCOMPARE(qualityRating(3, 4), 7.5);
        QCOMPARE(speedRating(0.0, false).value_or(-1), 10.0);
        QCOMPARE(speedRating(1.25, false).value_or(-1), 7.5);
        // A point more off when no text shows while you speak.
        QCOMPARE(speedRating(1.25, true).value_or(-1), 6.5);
        QCOMPARE(speedRating(6.0, false).value_or(-1), 0.0);
        QVERIFY(!speedRating(std::nullopt, false));
    }

    // The services' ratings, as the spec's evidence table works them out:
    // Accuracy or Quality, then Speed.
    void servicesRateAsTheirBenchmarksSay()
    {
        QCOMPARE(ratingValues(ProviderRole::Speech, QStringLiteral("codex")), QList<double>({8.5, 7}));
        QCOMPARE(ratingValues(ProviderRole::Speech, QStringLiteral("claude")), QList<double>({5.5, 10}));
        QCOMPARE(ratingValues(ProviderRole::Refinement, QStringLiteral("openai")), QList<double>({10, 7.5}));
        QCOMPARE(ratingValues(ProviderRole::Refinement, QStringLiteral("anthropic")), QList<double>({10, 7}));
        const auto measures = [](ProviderRole role, const QString &id) {
            QList<RatingMeasure> measures;
            const std::optional<ProviderRating> rating = providerRating(role, id, {}, {});
            for (const Rating &bar : rating->bars) {
                measures.append(bar.measure);
            }
            return measures;
        };
        QCOMPARE(measures(ProviderRole::Speech, QStringLiteral("codex")),
                 QList<RatingMeasure>({RatingMeasure::Accuracy, RatingMeasure::Speed}));
        QCOMPARE(measures(ProviderRole::Refinement, QStringLiteral("openai")),
                 QList<RatingMeasure>({RatingMeasure::Quality, RatingMeasure::Speed}));
        // A service names no model as what was rated.
        QCOMPARE(ratedModel(ProviderRole::Speech, QStringLiteral("codex"), referencePc()), QString());
    }

    // A refinement service is rated by the model the settings hold, and not
    // at all by one the benchmark never ran.
    void refinementServicesRateTheChosenModel()
    {
        AppSettings chosen;
        chosen.refinement.anthropicModel = QStringLiteral("claude-haiku-5-5");
        chosen.refinement.openAiModel = QStringLiteral("gpt-6.1-sol");
        QCOMPARE(ratingValues(ProviderRole::Refinement, QStringLiteral("anthropic"), {}, chosen), QList<double>({9, 8}));
        QCOMPARE(ratingValues(ProviderRole::Refinement, QStringLiteral("openai"), {}, chosen), QList<double>({9.5, 7}));
        // The GPT-5.4 models need an API key, which the benchmark had none of.
        chosen.refinement.openAiModel = QStringLiteral("gpt-5.4");
        QVERIFY(!providerRating(ProviderRole::Refinement, QStringLiteral("openai"), {}, chosen));
    }

    // The server and its model are the user's own, so there is nothing to rate.
    void customEndpointAndNoneHaveNoRating()
    {
        for (const ProviderRole role : {ProviderRole::Speech, ProviderRole::Refinement}) {
            QVERIFY(!providerRating(role, QStringLiteral("endpoint"), referencePc(), {}));
        }
        QVERIFY(!providerRating(ProviderRole::Refinement, QStringLiteral("none"), referencePc(), {}));
    }

    // Local Model rates the model suggested for this computer and Spoken
    // Language from its word error rate and the catalog's speed estimate, a
    // Speed Test where Runs on places models now, or the chosen model.
    void localModelRatesTheModelItWouldRun()
    {
        QCOMPARE(ratedModel(ProviderRole::Speech, QStringLiteral("local"), referencePc()), QStringLiteral("Parakeet 0.6B"));
        // 3.99% FLEURS, an estimated 3.66% AA-WER; 10 s of speech in 10/26.01 s on this chip.
        QCOMPARE(ratingValues(ProviderRole::Speech, QStringLiteral("local"), referencePc()), QList<double>({8.5, 9}));

        AppSettings settings;
        LocalSpeechSettings &local = settings.speech.local;
        local.speedTestSeconds.insert(localSpeedTestKey(QStringLiteral("parakeet"), {QStringLiteral("cpu"), {}}), 4.0);
        QCOMPARE(ratingValues(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings), QList<double>({8.5, 9}));
        local.speedTestSeconds.insert(localSpeedTestKey(QStringLiteral("parakeet"), local.runsOn), 2.0);
        QCOMPARE(ratingValues(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings), QList<double>({8.5, 6}));

        local.modelId = QStringLiteral("whisper-turbo");
        local.modelChosen = true;
        QCOMPARE(ratedModel(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings),
                 QStringLiteral("Whisper Large v3 Turbo"));
        // 4.62% AA-WER; 10/3.69 s, and a point off since text waits until you stop.
        QCOMPARE(ratingValues(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings), QList<double>({7.5, 3.5}));
    }

    // Each catalog model's own bars; a model with no speed figure for this
    // machine has no Speed, and none has one before the probe answers.
    void localModelsRateOnThisComputer()
    {
        const LocalSpeechSettings local;
        const auto values = [&local](const QString &id, const HardwareProfile &hardware) {
            QList<double> values;
            for (const Rating &bar : localModelRating(*findLocalModel(id), hardware, local)) {
                values.append(bar.value.value_or(-1));
            }
            return values;
        };
        QCOMPARE(values(QStringLiteral("moonshine-small"), referencePc()), QList<double>({4, 8.5}));
        QCOMPARE(values(QStringLiteral("qwen3-asr"), referencePc()), QList<double>({9, 3.5}));
        QCOMPARE(values(QStringLiteral("voxtral-small"), referencePc()), QList<double>({9, -1}));
        QCOMPARE(values(QStringLiteral("parakeet"), HardwareProfile{}), QList<double>({8.5, -1}));
        // Its own 4.5% AA-WER, behind Parakeet 0.6B as on FLEURS.
        QCOMPARE(values(QStringLiteral("parakeet-v3"), HardwareProfile{}), QList<double>({7.5, -1}));
    }

    // Until the hardware probe answers, the profile has no memory reading and
    // neither local provider is rated.
    void localProvidersRateNothingOnUnknownHardware()
    {
        for (const ProviderRole role : {ProviderRole::Speech, ProviderRole::Refinement}) {
            QVERIFY(!providerRating(role, QStringLiteral("local"), HardwareProfile{}, {}));
            QVERIFY(providerRating(role, QStringLiteral("local"), referencePc(), {}));
        }
    }

    // Local Runner rates the cleanup model suggested here, at this
    // hardware's latency for it.
    void localRunnerRatesTheSuggestedCleanupModel()
    {
        QCOMPARE(ratedModel(ProviderRole::Refinement, QStringLiteral("local"), processorOnly()), QStringLiteral("LFM2.5 1.2B"));
        QCOMPARE(ratingValues(ProviderRole::Refinement, QStringLiteral("local"), processorOnly()), QList<double>({5.5, 5}));
        QCOMPARE(ratedModel(ProviderRole::Refinement, QStringLiteral("local"), graphicsCard(12)), QStringLiteral("Gemma 4 E4B"));
        QCOMPARE(ratingValues(ProviderRole::Refinement, QStringLiteral("local"), graphicsCard(12)), QList<double>({6.5, 7}));
    }

    // The Model rows' suggestions start with the settings' defaults.
    void refinementServicesSuggestTheirDefaultFirst()
    {
        const RefinementSettings defaults;
        QCOMPARE(refinementServiceModels(QStringLiteral("openai")).first().id, defaults.openAiModel);
        QCOMPARE(refinementServiceModels(QStringLiteral("anthropic")).first().id, defaults.anthropicModel);
        QCOMPARE(refinementServiceModels(QStringLiteral("anthropic")).first().name, QStringLiteral("Claude Opus 5.5"));
        QVERIFY(refinementServiceModels(QStringLiteral("local")).isEmpty());
    }
};

int runProviderRegistryTests(int argc, char **argv)
{
    ProviderRegistryTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_provider_registry.moc"
