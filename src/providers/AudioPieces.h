#pragma once

#include "providers/SpeechAudio.h"

#include <QByteArrayView>

namespace speecher {

// Cuts a run of 16 kHz mono audio into pieces a speech service hears one at
// a time, as a long run fails some of them:
// - Moonshine Streaming's position table holds 81.9 s of frames, past which
//   the CPU aborts and a GPU reads beyond it.
// - Cohere returned half the words of 95 s of speech.
// - Codex dictation reads an utterance slower than a file streams in, and
//   drops all of its text once it lags 15 s behind the utterance's end
//   (docs/research/0004).
// A piece ends after the first kPauseBytes past kShortestPieceBytes that are
// all 20 dB under the piece's loudest, or at kLongestPieceBytes if none come.
class AudioPieces {
public:
    static constexpr qsizetype kShortestPieceBytes = qsizetype(20) * kSpeechBytesPerSecond;
    static constexpr qsizetype kLongestPieceBytes = qsizetype(40) * kSpeechBytesPerSecond;
    // 300 ms. A shorter quiet is often a dip inside a phrase, and a cut there
    // left the word beside it in both pieces' text, Parakeet's and Codex's.
    static constexpr qsizetype kPauseBytes = 3 * kSpeechChunkBytes;

    struct Fit {
        // How much of the audio, from its start, goes in the current piece.
        qsizetype bytes = 0;
        // Whether the piece ends there; the audio after starts the next.
        bool ends = false;
    };
    // The next audio of the run, which goes on from the last call's.
    Fit fit(QByteArrayView pcm);

private:
    qsizetype m_bytes = 0;
    // How much of the piece's end is quiet.
    qsizetype m_quietBytes = 0;
    // The loudest 100 ms of the piece so far, as RMS.
    float m_loudest = 0.0f;
};

} // namespace speecher
