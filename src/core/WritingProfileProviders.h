#pragma once

#include "core/AppSettings.h"

#include <optional>

namespace speecher {

// The speech settings the profile's own service runs with: its choice first,
// over the Speech to text page's entry for that service, then the page's
// whole order after it. Nullopt when the profile uses the page's order.
std::optional<SpeechSettings> profileSpeechSettings(const SpeechSettings &speech,
                                                    const WritingProfileSettings &profile);

// What a Dictation Session under this profile runs with: the profile's
// speech and refinement services, with their models, first in each order,
// then the Speech to text and Refinement pages' orders. A profile's speech
// service that can't listen for the Spoken Language is passed over, as an
// unsupported fallback is, and the page's order runs as it is. With Clean up
// my words off nothing refines, whatever the profile picks.
AppSettings withWritingProfileProviders(AppSettings settings, const WritingProfileSettings &profile);

} // namespace speecher
