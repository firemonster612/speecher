#pragma once

#include "platform/win/WinCaptureInput.h"

namespace speecher {

// What the speakers play, captured with WASAPI loopback from the default
// output as 16 kHz mono s16 like the microphone, and moved to the new default
// output when that changes. The poll that finds nothing played fills that
// stretch with silence.
class WinSystemAudioInput final : public WinCaptureInput {
public:
    explicit WinSystemAudioInput(QObject *parent = nullptr);
};

} // namespace speecher
