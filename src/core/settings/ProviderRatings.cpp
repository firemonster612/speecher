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

// Refinement services, from the benchmark in docs/provider-ratings-mockup/bench,
// run 2026-10-10 on each model the Model rows list, at its provider's default
// settings: OpenAI at effort none (low for GPT-6.1 Sol, which refuses none) on
// the Fast tier; Anthropic at effort low at standard speed, since its fast
// mode needs usage credits a subscription lacks and the app falls back. Checks
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
    {"claude-opus-5-5", 121, 1.41},
    {"claude-opus-5", 123, 1.94},
    {"claude-sonnet-5-5", 123, 1.23},
    {"claude-haiku-5-5", 110, 0.92},
    {"claude-haiku-4-5", 116, 0.89},
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

// LocalSetup reports a plain processor with no memory until its hardware probe
// answers, and for good in a build without local speech. Nothing is rated or
// suggested for such a computer: the answer would be for some other machine.
bool hardwareKnown(const HardwareProfile &hardware)
{
    return hardware.systemRamBytes > 0;
}

QList<Rating> localModelBars(const LocalModel &model, const HardwareProfile &hardware, const LocalSpeechSettings &local)
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

// As LocalSetup::speechModelChoice: the chosen model, else the suggestion.
const LocalModel &ratedLocalModel(const HardwareProfile &hardware, const SpeechSettings &speech)
{
    const LocalModel *chosen = findLocalModel(speech.local.modelId);
    return speech.local.modelChosen && chosen ? *chosen : suggestedLocalModel(hardware, speech.language);
}

// The latency table's row for this computer; none while the hardware is unknown.
std::optional<CleanupHardware> cleanupHardwareHere(const HardwareProfile &hardware)
{
    return hardwareKnown(hardware) ? std::optional(cleanupHardwareFor(hardware)) : std::nullopt;
}

std::optional<CleanupModel> suggestedCleanupModelHere(std::optional<CleanupHardware> hardware)
{
    return hardware ? suggestedCleanupModel(*hardware) : std::nullopt;
}

QList<Rating> cleanupModelBars(const CleanupModel &model, std::optional<CleanupHardware> hardware)
{
    return refinementBars(model.benchmarkChecksPassed,
                          hardware ? std::optional(cleanupSeconds(model, *hardware)) : std::nullopt);
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
    if (providerId == QStringLiteral("local") && hardwareKnown(hardware)) {
        const LocalModel &model = ratedLocalModel(hardware, speech);
        return ProviderRating{localModelBars(model, hardware, speech.local),
                              QStringLiteral("%1 on this computer").arg(model.name)};
    }
    return std::nullopt;
}

std::optional<ProviderRating> refinementRating(const QString &providerId, const HardwareProfile &hardware,
                                               const RefinementSettings &refinement)
{
    if (const std::optional<RowOption> model = refinementServiceModel(providerId, refinement)) {
        const auto measured = std::ranges::find_if(measuredRefinementModels, [&model](const auto &measured) {
            return model->id == QLatin1String(measured.id);
        });
        if (measured == std::ranges::end(measuredRefinementModels)) {
            return std::nullopt;
        }
        return ProviderRating{refinementBars(measured->checksPassed, measured->waitSeconds), {}};
    }
    if (providerId == QStringLiteral("local")) {
        const std::optional<CleanupHardware> cleanupHardware = cleanupHardwareHere(hardware);
        const std::optional<CleanupModel> suggested = suggestedCleanupModelHere(cleanupHardware);
        if (!suggested) {
            return std::nullopt;
        }
        return ProviderRating{cleanupModelBars(*suggested, cleanupHardware),
                              QStringLiteral("%1, suggested for this computer").arg(suggested->name)};
    }
    return std::nullopt;
}

QString serviceModelNote(bool isDefault)
{
    const QString change =
        QStringLiteral("Change it in Settings, under %1.").arg(paneTitle(QStringLiteral("refinement")));
    return isDefault ? QStringLiteral("The default. ") + change : change;
}

// A catalog as its Advanced list shows it: rate is a model's row and whether
// it is the one suggested here, which goes first and says so.
template <typename Catalog, typename Rate>
QList<RatedModel> suggestedFirst(const Catalog &catalog, Rate rate)
{
    QList<RatedModel> models;
    for (const auto &model : catalog) {
        auto [rated, isSuggested] = rate(model);
        if (isSuggested) {
            rated.note = localModelText(LocalModelText::Suggested);
            models.prepend(rated);
        } else {
            models.append(rated);
        }
    }
    return models;
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
    const LocalModel *suggested = hardwareKnown(hardware) ? &suggestedLocalModel(hardware, speech.language) : nullptr;
    return suggestedFirst(localModelCatalog(), [&](const LocalModel &model) {
        const bool isSuggested = suggested && suggested->id == model.id;
        return std::pair{RatedModel{model.name, {}, localModelBars(model, hardware, speech.local)}, isSuggested};
    });
}

QList<RatedModel> refinementModels(const QString &providerId, const HardwareProfile &hardware,
                                   const RefinementSettings &refinement)
{
    if (const std::optional<RowOption> model = refinementServiceModel(providerId, refinement)) {
        const bool isDefault = model->id == refinementServiceModel(providerId, RefinementSettings{})->id;
        return {{model->label, serviceModelNote(isDefault), {}}};
    }
    if (providerId != QStringLiteral("local")) {
        return {};
    }
    const std::optional<CleanupHardware> cleanupHardware = cleanupHardwareHere(hardware);
    const std::optional<CleanupModel> suggested = suggestedCleanupModelHere(cleanupHardware);
    return suggestedFirst(cleanupModelCatalog(), [&](const CleanupModel &model) {
        const bool isSuggested = suggested && suggested->ollamaTag == model.ollamaTag;
        return std::pair{RatedModel{model.name, {}, cleanupModelBars(model, cleanupHardware)}, isSuggested};
    });
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

QList<RatedModel> providerModels(ProviderRole role, const QString &providerId,
                                 const HardwareProfile &hardware, const AppSettings &settings)
{
    return role == ProviderRole::Speech ? speechModels(providerId, hardware, settings.speech)
                                        : refinementModels(providerId, hardware, settings.refinement);
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
