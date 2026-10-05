#include "core/settings/ProviderRatings.h"

#include "core/CleanupModelCatalog.h"
#include "core/settings/SettingsSchema.h"

#include <algorithm>

namespace speecher {
namespace {

// Speech services, from Artificial Analysis (AA), read 2026-10-05.
// ChatGPT Codex's final text comes from GPT Transcribe while Transcribe again
// for accuracy is on, the default: 3.31% AA-WER v2. You wait for GPT Live
// Transcribe's last phrase (0.81 s on AA's streaming leaderboard) and then
// that pass (0.77 s, timed on an 11 s clip against
// chatgpt.com/backend-api/transcribe).
constexpr double codexWordErrorRate = 3.31;
constexpr double codexWaitSeconds = 0.81 + 0.77;
// Claude Voice is Deepgram Nova 3 Realtime: 6.59% streaming AA-WER, its final
// text 0.07 s after you stop.
constexpr double claudeWordErrorRate = 6.59;
constexpr double claudeWaitSeconds = 0.07;

// Refinement services, from the benchmark in docs/provider-ratings-mockup/bench:
// both passed every check; the waits are median seconds. OpenAI is gpt-6-luna
// at effort none on the Fast tier; Anthropic is Claude Opus 5.5 at effort low
// at standard speed, since its fast mode needs usage credits a subscription
// lacks and the app falls back.
constexpr int cloudChecksPassed = kRefinementBenchmarkChecks;
constexpr double openAiWaitSeconds = 1.54;
constexpr double anthropicWaitSeconds = 2.77;

double toHalves(double rating)
{
    return qRound(std::clamp(rating, 0.0, 10.0) * 2) / 2.0;
}

QList<Rating> speechServiceBars(double wordErrorRate, double waitSeconds)
{
    // Both services write text while you speak.
    return {{RatingMeasure::Accuracy, accuracyRating(wordErrorRate)},
            {RatingMeasure::Speed, speedRating(waitSeconds, false)}};
}

QList<Rating> refinementBars(int checksPassed, double waitSeconds)
{
    // Refinement has no text to show while you speak, so the extra point
    // never comes off.
    return {{RatingMeasure::Quality, qualityRating(checksPassed, kRefinementBenchmarkChecks)},
            {RatingMeasure::Speed, speedRating(waitSeconds, false)}};
}

// AA-WER over FLEURS English, averaged over the catalog models Artificial
// Analysis lists by name, turns a FLEURS figure into an AA-WER estimate.
// Parakeet's AA figure is a sibling model's, so it stays out.
double fleursToArtificialAnalysis()
{
    const QStringList listed{QStringLiteral("whisper-turbo"), QStringLiteral("cohere"),
                             QStringLiteral("voxtral-small")};
    double sum = 0;
    for (const QString &id : listed) {
        const LocalModel *model = findLocalModel(id);
        Q_ASSERT_X(model, "fleursToArtificialAnalysis", qPrintable(id));
        sum += model->artificialAnalysisWer / model->fleursEnglishWer;
    }
    return sum / listed.size();
}

double ratedWordErrorRate(const LocalModel &model)
{
    return model.artificialAnalysisWer > 0 ? model.artificialAnalysisWer
                                           : model.fleursEnglishWer * fleursToArtificialAnalysis();
}

QList<Rating> localModelBars(const LocalModel &model, const HardwareProfile &hardware, const LocalSpeechSettings &local)
{
    std::optional<double> wait = measuredSpeedTestSeconds(local, model.id);
    if (!wait) {
        if (const std::optional<SpeedEstimate> estimate = estimatedSpeed(model, hardware)) {
            wait = estimate->secondsFor10sSpeech;
        }
    }
    // A streaming model has written most of its text by the time you stop,
    // so counting the whole figure overstates its wait. Kept simple on purpose.
    return {{RatingMeasure::Accuracy, accuracyRating(ratedWordErrorRate(model))},
            {RatingMeasure::Speed, speedRating(wait, !model.streams)}};
}

// As LocalSetup::speechModelChoice: the chosen model, else the suggestion.
const LocalModel &ratedLocalModel(const HardwareProfile &hardware, const SpeechSettings &speech)
{
    const LocalModel *chosen = findLocalModel(speech.local.modelId);
    return speech.local.modelChosen && chosen ? *chosen : suggestedLocalModel(hardware, speech.language);
}

QList<Rating> cleanupModelBars(const CleanupModel &model, CleanupHardware hardware)
{
    return refinementBars(model.benchmarkChecksPassed, cleanupSeconds(model, hardware));
}

std::optional<ProviderRating> speechRating(const QString &providerId, const HardwareProfile &hardware,
                                           const SpeechSettings &speech)
{
    if (providerId == QStringLiteral("codex")) {
        return ProviderRating{speechServiceBars(codexWordErrorRate, codexWaitSeconds), {}};
    }
    if (providerId == QStringLiteral("claude")) {
        return ProviderRating{speechServiceBars(claudeWordErrorRate, claudeWaitSeconds), {}};
    }
    if (providerId == QStringLiteral("local")) {
        const LocalModel &model = ratedLocalModel(hardware, speech);
        return ProviderRating{localModelBars(model, hardware, speech.local),
                              QStringLiteral("%1 on this computer").arg(model.name)};
    }
    return std::nullopt;
}

std::optional<ProviderRating> refinementRating(const QString &providerId, const HardwareProfile &hardware)
{
    if (providerId == QStringLiteral("openai")) {
        return ProviderRating{refinementBars(cloudChecksPassed, openAiWaitSeconds), {}};
    }
    if (providerId == QStringLiteral("anthropic")) {
        return ProviderRating{refinementBars(cloudChecksPassed, anthropicWaitSeconds), {}};
    }
    if (providerId == QStringLiteral("local")) {
        const CleanupHardware cleanupHardware = cleanupHardwareFor(hardware);
        const std::optional<CleanupModel> suggested = suggestedCleanupModel(cleanupHardware);
        if (!suggested) {
            return std::nullopt;
        }
        return ProviderRating{cleanupModelBars(*suggested, cleanupHardware),
                              QStringLiteral("%1, suggested for this computer").arg(suggested->name)};
    }
    return std::nullopt;
}

QString defaultModelNote()
{
    return QStringLiteral("The default. Change it in Settings, under %1.").arg(paneTitle(QStringLiteral("refinement")));
}

QList<RatedModel> speechModels(const QString &providerId, const HardwareProfile &hardware,
                               const SpeechSettings &speech)
{
    if (providerId == QStringLiteral("codex")) {
        return {
            // ui-lint: allow title-case: a model name.
            {QStringLiteral("GPT Live Transcribe"), QStringLiteral("Writes each phrase as you pause."), {}},
            // ui-lint: allow title-case: a model name.
            {QStringLiteral("GPT Transcribe"),
             QStringLiteral("Goes over the whole recording when you stop, while Transcribe again for "
                            "accuracy is on."),
             {}},
        };
    }
    if (providerId == QStringLiteral("claude")) {
        // ui-lint: allow title-case: a model name.
        return {{QStringLiteral("Deepgram Nova 3"), QStringLiteral("Writes your words as you speak."), {}}};
    }
    if (providerId != QStringLiteral("local")) {
        return {};
    }
    const LocalModel &suggested = suggestedLocalModel(hardware, speech.language);
    QList<RatedModel> models{{suggested.name, localModelText(LocalModelText::Suggested),
                              localModelBars(suggested, hardware, speech.local)}};
    for (const LocalModel &model : localModelCatalog()) {
        if (model.id != suggested.id) {
            models.append({model.name, QString(), localModelBars(model, hardware, speech.local)});
        }
    }
    return models;
}

QList<RatedModel> refinementModels(const QString &providerId, const HardwareProfile &hardware)
{
    if (providerId == QStringLiteral("openai")) {
        // The default model id is also its name.
        return {{RefinementSettings{}.openAiModel, defaultModelNote(), {}}};
    }
    if (providerId == QStringLiteral("anthropic")) {
        // RefinementSettings' default, claude-opus-5-5, by name.
        return {{QStringLiteral("Claude Opus 5.5"), defaultModelNote(), {}}};
    }
    if (providerId != QStringLiteral("local")) {
        return {};
    }
    const CleanupHardware cleanupHardware = cleanupHardwareFor(hardware);
    const std::optional<CleanupModel> suggested = suggestedCleanupModel(cleanupHardware);
    QList<RatedModel> models;
    for (const CleanupModel &model : cleanupModelCatalog()) {
        const bool isSuggested = suggested && suggested->ollamaTag == model.ollamaTag;
        const RatedModel rated{model.name, isSuggested ? localModelText(LocalModelText::Suggested) : QString(),
                               cleanupModelBars(model, cleanupHardware)};
        if (isSuggested) {
            models.prepend(rated);
        } else {
            models.append(rated);
        }
    }
    return models;
}

} // namespace

double accuracyRating(double wordErrorRatePercent)
{
    return toHalves(12 - wordErrorRatePercent);
}

double qualityRating(int checksPassed, int checks)
{
    Q_ASSERT_X(checks > 0, "qualityRating", "a benchmark needs checks");
    return toHalves(10.0 * checksPassed / checks);
}

std::optional<double> speedRating(std::optional<double> waitSeconds, bool textWaitsUntilYouStop)
{
    if (!waitSeconds) {
        return std::nullopt;
    }
    return toHalves(10 - 2 * *waitSeconds - (textWaitsUntilYouStop ? 1 : 0));
}

std::optional<ProviderRating> providerRating(ProviderRole role, const QString &providerId,
                                             const HardwareProfile &hardware, const AppSettings &settings)
{
    return role == ProviderRole::Speech ? speechRating(providerId, hardware, settings.speech)
                                        : refinementRating(providerId, hardware);
}

QList<RatedModel> providerModels(ProviderRole role, const QString &providerId,
                                 const HardwareProfile &hardware, const AppSettings &settings)
{
    return role == ProviderRole::Speech ? speechModels(providerId, hardware, settings.speech)
                                        : refinementModels(providerId, hardware);
}

QString ratingMeasureLabel(RatingMeasure measure)
{
    switch (measure) {
    case RatingMeasure::Accuracy:
        return QStringLiteral("Accuracy");
    case RatingMeasure::Quality:
        return QStringLiteral("Quality");
    case RatingMeasure::Speed:
        break;
    }
    return QStringLiteral("Speed");
}

QString ratingValueText(const Rating &rating)
{
    return rating.value ? QStringLiteral("%1/10").arg(*rating.value) : QStringLiteral("?");
}

QString providerModelsCaption()
{
    return QStringLiteral("Advanced");
}

QString modelColumnHeader()
{
    return QStringLiteral("Model");
}

} // namespace speecher
