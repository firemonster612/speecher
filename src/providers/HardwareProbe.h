#pragma once

#include "core/LocalModelCatalog.h"
#include "providers/LocalSpeechEngine.h"

namespace speecher {

// What the Local models page summarises and the Hardware Tier is computed
// from.
struct HardwareSummary {
    HardwareProfile profile;
    int cpuThreads = 0;
    QList<LocalSpeechEngine::Device> gpus;
};

// Loads transcribe.cpp's backends on first use, which can take a while; run it
// off the GUI thread.
HardwareSummary probeHardware();

} // namespace speecher
