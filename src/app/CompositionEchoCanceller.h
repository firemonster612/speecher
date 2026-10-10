#pragma once

#include "core/EchoCanceller.h"

#include <QString>

#include <memory>

namespace speecher {

// Speecher's own echo cancellation, webrtc-audio-processing's, for the
// compositions whose system does not cancel echo itself: none with why in
// warning where it was not built in or cannot start.
std::unique_ptr<EchoCanceller> createWebRtcEchoCanceller(QString *warning);

} // namespace speecher
