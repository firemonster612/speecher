#include "common/test_suites.h"
#include "app/ProviderSetup.h"
#include "core/CleanupModelCatalog.h"
#include "core/settings/ProviderRatings.h"
#include "core/settings/SettingsSchema.h"
#include "providers/ProviderRegistry.h"
#include "common/test_doubles.h"

using namespace speecher;
using namespace speecher::test;

namespace {

// "Accuracy 8.5/10", as a bar reads with its label.
QStringList barTexts(const QList<Rating> &bars)
{
    QStringList texts;
    for (const Rating &bar : bars) {
        texts.append(ratingMeasureLabel(bar.measure) + QLatin1Char(' ') + ratingValueText(bar));
    }
    return texts;
}

QStringList ratingTexts(ProviderRole role, const QString &id, const HardwareProfile &hardware = {},
                        const AppSettings &settings = {})
{
    const std::optional<ProviderRating> rating = providerRating(role, id, hardware, settings);
    return rating ? barTexts(rating->bars) : QStringList{QStringLiteral("none")};
}

QString ratedSubject(ProviderRole role, const QString &id, const HardwareProfile &hardware,
                     const AppSettings &settings = {})
{
    return providerRating(role, id, hardware, settings).value_or(ProviderRating{}).subject;
}

const RatedModel &modelNamed(const QList<RatedModel> &models, const QString &name)
{
    for (const RatedModel &model : models) {
        if (model.name == name) {
            return model;
        }
    }
    qFatal("no model %s", qPrintable(name));
}

QStringList modelNames(const QList<RatedModel> &models)
{
    QStringList names;
    for (const RatedModel &model : models) {
        names.append(model.name);
    }
    return names;
}

// The catalog's PC reference machine: a Ryzen 7 PRO 4750U with its Radeon
// graphics and room for every small model.
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

HardwareProfile appleChip(const QString &name)
{
    HardwareProfile mac;
    mac.accelerator = HardwareProfile::Accelerator::AppleSilicon;
    mac.chipName = name;
    mac.systemRamBytes = quint64(16) << 30;
    mac.gpuMemoryBytes = quint64(12) << 30;
    return mac;
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
        QCOMPARE(ratingValueText({RatingMeasure::Speed, 8.5}), QStringLiteral("8.5/10"));
        QCOMPARE(ratingValueText({RatingMeasure::Speed, 7.0}), QStringLiteral("7/10"));
        QCOMPARE(ratingValueText({RatingMeasure::Speed, std::nullopt}), QStringLiteral("?"));
    }

    // The services' ratings, as the spec's evidence table works them out.
    void servicesRateAsTheirBenchmarksSay()
    {
        QCOMPARE(ratingTexts(ProviderRole::Speech, QStringLiteral("codex")),
                 QStringList({QStringLiteral("Accuracy 8.5/10"), QStringLiteral("Speed 7/10")}));
        QCOMPARE(ratingTexts(ProviderRole::Speech, QStringLiteral("claude")),
                 QStringList({QStringLiteral("Accuracy 5.5/10"), QStringLiteral("Speed 10/10")}));
        QCOMPARE(ratingTexts(ProviderRole::Refinement, QStringLiteral("openai")),
                 QStringList({QStringLiteral("Quality 10/10"), QStringLiteral("Speed 7/10")}));
        QCOMPARE(ratingTexts(ProviderRole::Refinement, QStringLiteral("anthropic")),
                 QStringList({QStringLiteral("Quality 10/10"), QStringLiteral("Speed 4.5/10")}));
        // A service names no model as what was rated.
        QCOMPARE(ratedSubject(ProviderRole::Speech, QStringLiteral("codex"), referencePc()), QString());

        QCOMPARE(modelNames(providerModels(ProviderRole::Speech, QStringLiteral("codex"), {}, {})),
                 QStringList({QStringLiteral("GPT Live Transcribe"), QStringLiteral("GPT Transcribe")}));
        QCOMPARE(modelNames(providerModels(ProviderRole::Speech, QStringLiteral("claude"), {}, {})),
                 QStringList({QStringLiteral("Deepgram Nova 3")}));
        const QList<RatedModel> openAi = providerModels(ProviderRole::Refinement, QStringLiteral("openai"), {}, {});
        QCOMPARE(modelNames(openAi), QStringList({QStringLiteral("gpt-6-luna")}));
        QCOMPARE(openAi.first().note, QStringLiteral("The default. Change it in Settings, under Refinement."));
        QVERIFY(openAi.first().bars.isEmpty());
        QCOMPARE(modelNames(providerModels(ProviderRole::Refinement, QStringLiteral("anthropic"), {}, {})),
                 QStringList({QStringLiteral("Claude Opus 5.5")}));
    }

    // The server and its model are the user's own, so there is nothing to rate.
    void customEndpointHasNoRatingsOrModels()
    {
        for (const ProviderRole role : {ProviderRole::Speech, ProviderRole::Refinement}) {
            QVERIFY(!providerRating(role, QStringLiteral("endpoint"), referencePc(), {}));
            QVERIFY(providerModels(role, QStringLiteral("endpoint"), referencePc(), {}).isEmpty());
        }
        QVERIFY(!providerRating(ProviderRole::Refinement, QStringLiteral("none"), referencePc(), {}));
    }

    // Local Model rates the model suggested for this computer and Spoken
    // Language, from its word error rate and the catalog's speed estimate.
    void localModelRatesTheSuggestionFromItsEstimate()
    {
        QCOMPARE(ratedSubject(ProviderRole::Speech, QStringLiteral("local"), referencePc()),
                 QStringLiteral("Parakeet 0.6B on this computer"));
        // 6.43% AA-WER; 10 s of speech in 10/26.01 s on this chip.
        QCOMPARE(ratingTexts(ProviderRole::Speech, QStringLiteral("local"), referencePc()),
                 QStringList({QStringLiteral("Accuracy 5.5/10"), QStringLiteral("Speed 9/10")}));
    }

    // A Speed Test's measurement where Runs on places models now replaces
    // the estimate; one from another placement does not.
    void localModelSpeedPrefersTheSpeedTest()
    {
        AppSettings settings;
        LocalSpeechSettings &local = settings.speech.local;
        local.speedTestSeconds.insert(localSpeedTestKey(QStringLiteral("parakeet"), {QStringLiteral("cpu"), {}}), 4.0);
        QCOMPARE(ratingTexts(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings),
                 QStringList({QStringLiteral("Accuracy 5.5/10"), QStringLiteral("Speed 9/10")}));
        local.speedTestSeconds.insert(localSpeedTestKey(QStringLiteral("parakeet"), local.runsOn), 2.0);
        QCOMPARE(ratingTexts(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings),
                 QStringList({QStringLiteral("Accuracy 5.5/10"), QStringLiteral("Speed 6/10")}));
    }

    // Once a model is chosen, that is the one rated.
    void localModelRatesTheChosenModel()
    {
        AppSettings settings;
        settings.speech.local.modelId = QStringLiteral("whisper-turbo");
        settings.speech.local.modelChosen = true;
        QCOMPARE(ratedSubject(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings),
                 QStringLiteral("Whisper Large v3 Turbo on this computer"));
        // 4.62% AA-WER; 10/3.69 s, and a point off since text waits until you stop.
        QCOMPARE(ratingTexts(ProviderRole::Speech, QStringLiteral("local"), referencePc(), settings),
                 QStringList({QStringLiteral("Accuracy 7.5/10"), QStringLiteral("Speed 3.5/10")}));
    }

    // Advanced lists every catalog model with its own bars, the suggestion
    // first. Models Artificial Analysis does not list are estimated from
    // FLEURS English, and a model without a speed figure here shows "?".
    void localModelAdvancedListsTheCatalogWithBars()
    {
        const QList<RatedModel> models =
            providerModels(ProviderRole::Speech, QStringLiteral("local"), referencePc(), {});
        QCOMPARE(models.size(), localModelCatalog().size());
        QCOMPARE(models.first().name, QStringLiteral("Parakeet 0.6B"));
        QCOMPARE(models.first().note, QStringLiteral("Suggested for this computer"));
        for (const RatedModel &model : models.mid(1)) {
            QVERIFY2(model.note.isEmpty(), qPrintable(model.name));
        }
        const QHash<QString, QStringList> expected{
            {QStringLiteral("Moonshine Small"), {QStringLiteral("Accuracy 4/10"), QStringLiteral("Speed 8.5/10")}},
            {QStringLiteral("Moonshine Medium"), {QStringLiteral("Accuracy 5/10"), QStringLiteral("Speed 8/10")}},
            {QStringLiteral("Parakeet 0.6B"), {QStringLiteral("Accuracy 5.5/10"), QStringLiteral("Speed 9/10")}},
            {QStringLiteral("Whisper Large v3 Turbo"),
             {QStringLiteral("Accuracy 7.5/10"), QStringLiteral("Speed 3.5/10")}},
            {QStringLiteral("Qwen3-ASR 1.7B"), {QStringLiteral("Accuracy 9/10"), QStringLiteral("Speed 3.5/10")}},
            {QStringLiteral("Cohere Transcribe"), {QStringLiteral("Accuracy 7.5/10"), QStringLiteral("Speed 6.5/10")}},
            {QStringLiteral("Voxtral Small 24B"), {QStringLiteral("Accuracy 9/10"), QStringLiteral("Speed ?")}},
        };
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QCOMPARE(barTexts(modelNamed(models, it.key()).bars), it.value());
        }
    }

    // Local Runner rates the cleanup model suggested here, at this
    // hardware's latency for it, and lists both models with the suggestion
    // first.
    void localRunnerRatesTheSuggestedCleanupModel()
    {
        QCOMPARE(ratedSubject(ProviderRole::Refinement, QStringLiteral("local"), processorOnly()),
                 QStringLiteral("LFM2.5 1.2B, suggested for this computer"));
        QCOMPARE(ratingTexts(ProviderRole::Refinement, QStringLiteral("local"), processorOnly()),
                 QStringList({QStringLiteral("Quality 5.5/10"), QStringLiteral("Speed 5/10")}));
        const QList<RatedModel> onProcessor =
            providerModels(ProviderRole::Refinement, QStringLiteral("local"), processorOnly(), {});
        QCOMPARE(modelNames(onProcessor), QStringList({QStringLiteral("LFM2.5 1.2B"), QStringLiteral("Gemma 4 E4B")}));
        QCOMPARE(onProcessor.at(0).note, QStringLiteral("Suggested for this computer"));
        QCOMPARE(onProcessor.at(1).note, QString());
        QCOMPARE(barTexts(onProcessor.at(1).bars),
                 QStringList({QStringLiteral("Quality 6.5/10"), QStringLiteral("Speed 0/10")}));

        QCOMPARE(ratedSubject(ProviderRole::Refinement, QStringLiteral("local"), graphicsCard(12)),
                 QStringLiteral("Gemma 4 E4B, suggested for this computer"));
        QCOMPARE(ratingTexts(ProviderRole::Refinement, QStringLiteral("local"), graphicsCard(12)),
                 QStringList({QStringLiteral("Quality 6.5/10"), QStringLiteral("Speed 7/10")}));
        const QList<RatedModel> onCard =
            providerModels(ProviderRole::Refinement, QStringLiteral("local"), graphicsCard(12), {});
        QCOMPARE(modelNames(onCard), QStringList({QStringLiteral("Gemma 4 E4B"), QStringLiteral("LFM2.5 1.2B")}));
        QCOMPARE(barTexts(onCard.at(1).bars),
                 QStringList({QStringLiteral("Quality 5.5/10"), QStringLiteral("Speed 9/10")}));
    }

    // No suggestion, no rating; Advanced still lists both models. Today's
    // latency table suggests a model for every class of machine, so this
    // holds the rating to the suggestion on each of them.
    void localRunnerRatesOnlyWhatItSuggests()
    {
        for (const HardwareProfile &hardware :
             {processorOnly(), referencePc(), graphicsCard(12), graphicsCard(4), appleChip(QStringLiteral("Apple M1")),
              appleChip(QStringLiteral("Apple M3 Pro")), appleChip(QStringLiteral("Apple M4 Max"))}) {
            const std::optional<CleanupModel> suggested = suggestedCleanupModel(cleanupHardwareFor(hardware));
            const std::optional<ProviderRating> rating =
                providerRating(ProviderRole::Refinement, QStringLiteral("local"), hardware, {});
            QCOMPARE(rating.has_value(), suggested.has_value());
            if (suggested) {
                QCOMPARE(rating->subject, QStringLiteral("%1, suggested for this computer").arg(suggested->name));
            }
            QCOMPARE(providerModels(ProviderRole::Refinement, QStringLiteral("local"), hardware, {}).size(), 2);
        }
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
