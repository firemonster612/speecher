#pragma once

// The one resolver for a run: what a Dictation Session, a Transcribe batch or
// a headless run writes with, settled layer by layer (Defaults → App Type →
// App → Writing Profile → this run), with where each value came from.

#include "core/AppSettings.h"
#include "core/Apps.h"
#include "core/Target.h"
#include "dictation/RunOverrides.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace speecher {

struct DictationPlan {
    // The Target's App Type, Writing Profile and Pasting choice before the
    // run's own.
    AppResolution app;
    // The Writing Profile the run writes with, its id even when no saved
    // profile holds it, with the run's tone and cleanup level over its own.
    WritingProfileSettings profile;
    // The saved settings with every layer applied: the speech and refinement
    // Service Orders as the run walks them (refinement's chain empty when it
    // does not refine), the Spoken Language, the refinement style for the
    // profile's cleanup level, the output format, the vocabulary the run
    // keeps and speech.vocabulary, the terms the speech request carries.
    AppSettings settings;
    // The run's own terms, which lead the saved ones.
    QStringList addedVocabulary;
    // Where each value came from, by field: writingProfile, pasting,
    // outputFormat, speech, refinement, spokenLanguage, cleanup, tone,
    // outputLanguage, instructions, vocabulary. A source is "run",
    // "profile:<id>", an App's id ("app:<uuid>"), "builtin" (the catalog's
    // profile for a known app), "type:<App Type id>" or "default" (the
    // field's own page: Other apps for the profile, the steps for pasting).
    QHash<QString, QString> sources;
};

// Whether this build offers a speech service. A Writing Profile's first
// speech service it lacks is passed over; null offers every one.
using OffersSpeech = std::function<bool(const QString &service)>;

// The plan for a run into target with these overrides. A Writing Profile's
// first services lead its Service Orders; its speech service is passed over
// when it can't listen for the run's Spoken Language or isn't offered. A run
// naming a service leads with it, keeping the profile's choice for it when
// the profile leads with the same service. With Clean up my words off nothing
// refines unless the run names a service.
DictationPlan planDictation(const AppSettings &settings,
                            const Target &target,
                            const RunOverrides &overrides,
                            const OffersSpeech &offersSpeech = {});

} // namespace speecher
