#pragma once

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

// One bar, out of 10 in halves. No value means no figure to rate from, shown
// as "?" in place of the bar.
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
    // What was rated, for a provider that runs on this computer: "Parakeet
    // 0.6B on this computer". Empty for a service.
    QString subject;
};

// One model behind a provider, as its Advanced disclosure lists it.
struct RatedModel {
    QString name;
    // What it does, or "Suggested for this computer"; may be empty.
    QString note;
    // A model on this computer has its own bars; a service's models have none.
    QList<Rating> bars;
    bool operator==(const RatedModel &) const = default;
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

// The models behind a provider, for its Advanced disclosure. A refinement
// service lists the model the settings hold. A Local Model lists the whole
// catalog, the suggested model first; a Local Runner lists both cleanup
// models, the suggested one first. While the hardware is unknown, neither
// suggests a model and a Speed without a Speed Test result is "?". Empty for
// Custom Endpoint, "none" and an unknown provider.
QList<RatedModel> providerModels(ProviderRole role, const QString &providerId,
                                 const HardwareProfile &hardware, const AppSettings &settings);

// "Accuracy", "Quality", "Speed".
QString ratingMeasureLabel(RatingMeasure measure);
// "8.5/10", or "?" without a value.
QString ratingValueText(const Rating &rating);
// The disclosure that lists a provider's models, in setup and Settings.
QString providerModelsCaption();
// The heading over the model names when the list has bars beside them.
QString modelColumnHeader();

} // namespace speecher
