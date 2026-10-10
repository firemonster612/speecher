#include "app/CompositionEchoCanceller.h"

#ifdef SPEECHER_WITH_WEBRTC_AEC
#include "platform/audio/WebRtcEchoCanceller.h"
#endif
#include "recording/RecordingPresentation.h"

namespace speecher {

std::unique_ptr<EchoCanceller> createWebRtcEchoCanceller(QString *warning)
{
#ifdef SPEECHER_WITH_WEBRTC_AEC
    int error = 0;
    std::unique_ptr<EchoCanceller> canceller = WebRtcEchoCanceller::create(&error);
    if (!canceller && warning) {
        *warning = echoCancellationFailedText(error);
    }
    return canceller;
#else
    if (warning) {
        *warning = echoCancellationNotBuiltText();
    }
    return nullptr;
#endif
}

} // namespace speecher
