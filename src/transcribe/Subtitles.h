#pragma once

#include "dictation/DictationPorts.h"

#include <QList>
#include <QString>

namespace speecher {

// What Export writes: the transcript as text, or its timings as subtitles.
enum class TranscriptFormat { Text, Srt, WebVtt };

// "txt", "srt" or "vtt".
QString transcriptFileExtension(TranscriptFormat format);

// segments as an SRT or WebVTT file. Every segment starts a new cue; one too
// long to read at once is split at sentence ends, then at commas, then
// between words, and its time shared out by length. No cue is shorter than
// half a second. Ends without a newline, as text does before the writer adds
// one.
QString subtitleFile(const QList<TranscriptSegment> &segments, TranscriptFormat format);

} // namespace speecher
