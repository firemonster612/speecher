#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/Subtitles.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace speecher {

class ProviderRegistry;

// What file transcription says about its progress and results, and how it
// exports them, for `speecher transcribe` and the file transcription session.

enum class TranscribePhase { Reading, Transcribing, Finishing, Refining };
QString transcribePhaseLabel(TranscribePhase phase);

// How far a file is through all of its work, 0..1, for its progress bar:
// reading the audio, sending it, waiting for the final text, refining.
// fractionSent is fileProgress's value. msInPhase is how long the file has
// been in its phase; it eases the open-ended waits (reading, finishing,
// refining) toward the end of their share without passing it. The result
// stays below 1: a front end shows 1 when fileFinished arrives, once the
// transcript is refined and saved.
qreal overallFileProgress(qreal fractionSent, TranscribePhase phase, bool refines, qint64 msInPhase);

// What a file's progress bar shows: the highest progress it has reached, so a
// speech stream that restarts mid-file never moves the bar back. One per
// file; start a new one when the next file starts.
struct ForwardProgress {
    qreal highest = 0.0;
    qreal advance(qreal progress)
    {
        if (progress > highest) {
            highest = progress;
        }
        return highest;
    }
};

// "Could not save <path>: <reason>", for a transcript that failed to write.
QString transcriptSaveError(const QString &path, const QString &reason);

// The model a refinement provider is set to use, or empty for none.
QString refinementModel(const QString &providerId, const RefinementSettings &settings);
// Whether a batch with these options refines its transcripts, which is when
// the results offer the raw text beside the refined one. The settings say
// whether the batch's profile translates, which refines even at None.
bool refinesTranscripts(const TranscribeOptions &options, const RefinementSettings &settings);
// The raw transcript when asked for or when no refined one came back.
QString shownTranscript(const TranscribeFileResult &result, bool raw);
// Whether a finished transcript exports in that format: text always,
// subtitles only when the speech provider returned timings.
bool canExportAs(const TranscribeFileResult &result, TranscriptFormat format);
// What Export writes: the shown transcript as text. Subtitles always come
// from the Raw Transcript, which the timings belong to.
QString exportedTranscript(const TranscribeFileResult &result, TranscriptFormat format, bool raw);

// What the results summary names the batch's choices, read when it starts so
// a later settings change does not rewrite a finished batch's description.
struct TranscribeBatchLabels {
    QString speech;
    // Provider and model; empty when the batch does not refine.
    QString refinement;
};
TranscribeBatchLabels batchLabels(const TranscribeOptions &options,
                                  const ProviderRegistry &providers,
                                  const RefinementSettings &settings);
// Why a speech provider's transcripts cannot be subtitles: it returned no
// timings.
QString subtitlesNeedTimings(const QString &speechProvider);

} // namespace speecher
