#include "core/ProviderRatings.h"

#include "core/CleanupModelCatalog.h"

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

// Refinement services, from the benchmark in tools/refinement-benchmark,
// run 2026-10-10 on each suggested model that a ChatGPT or Claude sign-in
// reaches, at its provider's default settings: OpenAI at effort none (low for
// GPT-6.1 Sol, which refuses none) on the Fast tier; Anthropic with adaptive
// thinking at effort low where the model takes it, at standard speed, since
// its fast mode needs usage credits a subscription lacks and the app falls
// back. The GPT-5.4 models need an API key, so they have no rating. Checks
// passed, then the median wait in seconds.
struct MeasuredRefinementModel {
    const char *id;
    int checksPassed;
    double waitSeconds;
};
constexpr MeasuredRefinementModel measuredRefinementModels[] = {
    {"gpt-6-luna", 122, 1.29},
    {"gpt-6.1-sol", 119, 1.53},
    {"gpt-6-astra", 120, 1.65},
    {"gpt-5.6-luna", 120, 1.53},
    {"gpt-5.6-terra", 123, 1.62},
    {"gpt-5.5", 123, 1.38},
    {"claude-opus-5-5", 121, 1.41},
    {"claude-opus-5", 123, 1.94},
    {"claude-sonnet-5-5", 123, 1.23},
    {"claude-haiku-5-5", 110, 0.92},
};

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

QList<Rating> refinementBars(int checksPassed, std::optional<double> waitSeconds)
{
    // Refinement has no text to show while you speak, so the extra point
    // never comes off.
    return {{RatingMeasure::Quality, qualityRating(checksPassed, kRefinementBenchmarkChecks)},
            {RatingMeasure::Speed, speedRating(waitSeconds, false)}};
}

// AA-WER over FLEURS English, averaged over the catalog models Artificial
// Analysis lists by name, turns a FLEURS figure into an AA-WER estimate.
double fleursToArtificialAnalysis()
{
    const QStringList listed{QStringLiteral("parakeet-v3"), QStringLiteral("whisper-turbo"),
                             QStringLiteral("cohere"), QStringLiteral("voxtral-small")};
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

// LocalSetup reports a plain processor with no memory until its hardware probe
// answers, and for good in a build without local speech. Nothing is rated or
// suggested for such a computer: the answer would be for some other machine.
bool hardwareKnown(const HardwareProfile &hardware)
{
    return hardware.systemRamBytes > 0;
}

// As LocalSetup::speechModelChoice: the chosen model, else the suggestion.
const LocalModel &ratedLocalModel(const HardwareProfile &hardware, const SpeechSettings &speech)
{
    const LocalModel *chosen = findLocalModel(speech.local.modelId);
    return speech.local.modelChosen && chosen ? *chosen : suggestedLocalModel(hardware, speech.language);
}

std::optional<ProviderRating> speechRating(const QString &providerId, const HardwareProfile &hardware,
                                           const SpeechSettings &speech)
{
    const std::optional<ProviderId> provider = parseProviderId(providerId);
    if (provider == ProviderId::Codex) {
        return ProviderRating{speechServiceBars(codexWordErrorRate, codexWaitSeconds), {}};
    }
    if (provider == ProviderId::Claude) {
        return ProviderRating{speechServiceBars(claudeWordErrorRate, claudeWaitSeconds), {}};
    }
    if (provider == ProviderId::Local && hardwareKnown(hardware)) {
        const LocalModel &model = ratedLocalModel(hardware, speech);
        return ProviderRating{localModelRating(model, hardware, speech.local), model.name};
    }
    return std::nullopt;
}

// The model a refinement service runs, from the settings, or nothing for a
// provider that is not a service.
std::optional<QString> refinementServiceModel(const QString &providerId, const RefinementSettings &refinement)
{
    const std::optional<ProviderId> provider = parseProviderId(providerId);
    if (provider == ProviderId::OpenAi) {
        return refinement.openAiModel;
    }
    if (provider == ProviderId::Anthropic) {
        return refinement.anthropicModel;
    }
    return std::nullopt;
}

std::optional<ProviderRating> refinementRating(const QString &providerId, const HardwareProfile &hardware,
                                               const RefinementSettings &refinement)
{
    if (const std::optional<QString> model = refinementServiceModel(providerId, refinement)) {
        const auto measured = std::find_if(std::cbegin(measuredRefinementModels), std::cend(measuredRefinementModels),
                                           [&model](const MeasuredRefinementModel &entry) {
                                               return *model == QLatin1String(entry.id);
                                           });
        if (measured == std::cend(measuredRefinementModels)) {
            return std::nullopt;
        }
        return ProviderRating{refinementBars(measured->checksPassed, measured->waitSeconds), {}};
    }
    if (parseProviderId(providerId) != ProviderId::Local || !hardwareKnown(hardware)) {
        return std::nullopt;
    }
    const CleanupHardware cleanupHardware = cleanupHardwareFor(hardware);
    const std::optional<CleanupModel> suggested = suggestedCleanupModel(cleanupHardware);
    if (!suggested) {
        return std::nullopt;
    }
    return ProviderRating{refinementBars(suggested->benchmarkChecksPassed, cleanupSeconds(*suggested, cleanupHardware)),
                          suggested->name};
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
                                        : refinementRating(providerId, hardware, settings.refinement);
}

QList<Rating> localModelRating(const LocalModel &model, const HardwareProfile &hardware,
                               const LocalSpeechSettings &local)
{
    std::optional<double> wait = measuredSpeedTestSeconds(local, model.id);
    if (!wait && hardwareKnown(hardware)) {
        if (const std::optional<SpeedEstimate> estimate = estimatedSpeed(model, hardware)) {
            wait = estimate->secondsFor10sSpeech;
        }
    }
    // A streaming model has written most of its text by the time you stop,
    // so counting the whole figure overstates its wait. Kept simple on purpose.
    return {{RatingMeasure::Accuracy, accuracyRating(ratedWordErrorRate(model))},
            {RatingMeasure::Speed, speedRating(wait, !model.streams)}};
}

const QList<RefinementModel> &refinementServiceModels(const QString &providerId)
{
    // The ids are the names OpenAI uses for its models.
    static const QList<RefinementModel> openAi = [] {
        QList<RefinementModel> models;
        for (const char *id : {"gpt-6-luna", "gpt-6.1-sol", "gpt-6-astra", "gpt-5.6-luna", "gpt-5.6-terra", "gpt-5.5",
                               "gpt-5.4-nano", "gpt-5.4-mini", "gpt-5.4"}) {
            models.append({QLatin1String(id), QLatin1String(id)});
        }
        return models;
    }();
    static const QList<RefinementModel> anthropic{
        {QStringLiteral("claude-opus-5-5"), QStringLiteral("Claude Opus 5.5")},
        {QStringLiteral("claude-opus-5"), QStringLiteral("Claude Opus 5")},
        {QStringLiteral("claude-sonnet-5-5"), QStringLiteral("Claude Sonnet 5.5")},
        {QStringLiteral("claude-haiku-5-5"), QStringLiteral("Claude Haiku 5.5")},
    };
    static const QList<RefinementModel> none;
    const std::optional<ProviderId> provider = parseProviderId(providerId);
    if (provider == ProviderId::OpenAi) {
        return openAi;
    }
    return provider == ProviderId::Anthropic ? anthropic : none;
}

} // namespace speecher
