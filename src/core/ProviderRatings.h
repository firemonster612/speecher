#pragma once

// How each provider rates out of 10, worked out from the benchmarks in
// ProviderRatings.cpp, and the models the refinement services offer. Numbers
// and names only: the UI words them.

#include "core/AppSettings.h"
#include "core/LocalModelCatalog.h"
#include "core/ProviderChain.h"

#include <QList>
#include <QString>

#include <optional>

namespace speecher {

// What a rating bar measures: Accuracy for speech, Quality for refinement,
// and Speed for both.
enum class RatingMeasure {
    Accuracy,
    Quality,
    Speed,
};

// One bar, out of 10 in halves. No value means no figure to rate from.
struct Rating {
    RatingMeasure measure = RatingMeasure::Speed;
    std::optional<double> value;

    bool operator==(const Rating &) const = default;
};

// The rules, one per measure, each rounded to the nearest half and kept
// within 0..10. Accuracy takes a point off per percentage point of words
// wrong above 2%, about the best any service scores.
double accuracyRating(double wordErrorRatePercent);
// The share of benchmark checks passed, out of 10.
double qualityRating(int checksPassed, int checks);
// Two points off per second you wait after you stop, for about 10 s of
// speech, and one more when no text shows while you speak. No wait figure
// gives no rating.
std::optional<double> speedRating(std::optional<double> waitSeconds, bool textWaitsUntilYouStop);

// The bars beside a provider: Accuracy or Quality, then Speed.
struct ProviderRating {
    QList<Rating> bars;
    // The model rated, for a provider that runs on this computer: the Local
    // Model's name, or the cleanup model suggested for this computer. Empty
    // for a service.
    QString ratedModel;
};

// A provider's rating, given this computer and the settings: a refinement
// service is the model the settings hold; a Local Model is the chosen one, or
// the suggested one for the Spoken Language until one is chosen, with its
// Speed Test result where it has one; a Local Runner is the cleanup model
// suggested for this computer. Empty for Custom Endpoint, "none", an unknown
// provider, a refinement service model the benchmark did not measure, a Local
// Runner where no cleanup model is suggested, and both local providers while
// the hardware is unknown (no memory reading, as LocalSetup's profile has
// until its probe answers).
std::optional<ProviderRating> providerRating(ProviderRole role, const QString &providerId,
                                             const HardwareProfile &hardware, const AppSettings &settings);

// A Local Model's own bars on this computer, as the Local models page shows
// each: its Speed Test where Runs on places models now, else the estimate for
// this machine, and no Speed figure while the hardware is unknown.
QList<Rating> localModelRating(const LocalModel &model, const HardwareProfile &hardware,
                               const LocalSpeechSettings &local);

// A model a refinement service offers: the id its API takes, and its name.
struct RefinementModel {
    QString id;
    QString name;
};

// The models OpenAI and Anthropic refinement suggest, their default first;
// empty for any other provider. A person may type any other id.
const QList<RefinementModel> &refinementServiceModels(const QString &providerId);

} // namespace speecher
