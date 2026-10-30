#pragma once

// Scratch-branch-only E2E stubs: a transcriber and refiner with deterministic
// text and timings slow enough that every dictation-panel phase is visible.

#include "providers/SpeechTranscriber.h"
#include "providers/TranscriptRefiner.h"

namespace speecher {

SpeechTranscriber *createE2ESpeechTranscriber(QObject *parent);
TranscriptRefiner *createE2ETranscriptRefiner(QObject *parent);

} // namespace speecher
