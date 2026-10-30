#pragma once

#include <QtGlobal>

namespace speecher {

// The audio a speech provider takes, 16 kHz mono s16, and how fast a stream
// sends audio that is already recorded.

constexpr int kSpeechBytesPerSecond = 16000 * 2;
// 100 ms, the chunk size live capture sends.
constexpr qsizetype kSpeechChunkBytes = kSpeechBytesPerSecond / 10;
// One chunk every 12 ms, about 8.3 times real time, for a file or audio that
// waited for its stream. Faster-than-real-time streaming is unproven for
// Claude Voice, which also caps what it buffers before its socket connects at
// 4 MB; at this rate even its 10 s connection timeout queues only about 83 s
// of audio (2.7 MB), so the cap is never reached.
constexpr int kSpeechCatchUpIntervalMs = 12;

} // namespace speecher
