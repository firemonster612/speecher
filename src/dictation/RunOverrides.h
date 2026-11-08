#pragma once

// Run Overrides: what one Dictation Session, Transcribe batch or headless run
// changes for itself, ahead of the saved settings. Never saved. An unset
// field is "Same as" the wider layer; planDictation (DictationPlan.h) applies
// them last.

#include "core/OutputFormat.h"
#include "core/ServiceOrder.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

struct RunOverrides {
    // A Writing Profile id, ahead of what the Target's App gives.
    std::optional<QString> writingProfile;
    // A tone and a cleanup level id, over the profile's own.
    std::optional<QString> tone;
    std::optional<QString> cleanupLevel;
    // A language code or kAutomaticSpokenLanguage, over the Spoken Language.
    std::optional<QString> spokenLanguage;
    // The service the run puts first, with what it asks of it. Naming the
    // service the profile already puts first keeps the profile's choice for
    // it under the run's; naming another leads the page's Service Order.
    std::optional<ServiceChoice> speech;
    // As speech, and it refines even with Clean up my words off. A service of
    // kNoRefinementProvider refines nothing.
    std::optional<ServiceChoice> refinement;
    // False leaves out the saved vocabulary, Learned Corrections and
    // replacements.
    bool applyVocabulary = true;
    // Terms the run adds, such as --vocab-file's, in order. They lead the
    // speech request and refinement, even without applyVocabulary.
    QStringList addedVocabulary;
    std::optional<OutputFormat> outputFormat;

    bool operator==(const RunOverrides &other) const = default;
};

} // namespace speecher
