#pragma once

#include "core/AppSettings.h"

#include <QString>

#include <optional>

namespace speecher {

// Whether a Writing Profile that picks this provider may pick its model too:
// a Local Model for speech, an OpenAI or Anthropic model for refinement. The
// other providers use the model their settings page names.
bool profilePicksSpeechModel(const QString &providerId);
bool profilePicksRefinementModel(const QString &providerId);

// The speech settings the profile's own service runs with: its provider and
// model over the Dictation page's settings. Nullopt when the profile uses the
// Dictation page's choice.
std::optional<SpeechSettings> profileSpeechSettings(const SpeechSettings &speech,
                                                    const WritingProfileSettings &profile);

// What a Dictation Session under this profile runs with: the profile's
// speech service and refinement provider, with their models, first in each
// chain, then the Dictation and Refinement pages' chains. A profile's speech
// service that can't listen for the Spoken Language is passed over, as an
// unsupported fallback is, and the Dictation page's chain runs as it is.
AppSettings withWritingProfileProviders(AppSettings settings, const WritingProfileSettings &profile);

} // namespace speecher
