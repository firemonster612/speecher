#pragma once

#include "transcribe/FileTranscriptionSession.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace speecher {

class ProviderRegistry;

// The wording of the Transcribe surfaces, shared so the three front ends say
// the same things. Only text lives here; each front end keeps its own widgets.

enum class TranscribePhase { Reading, Transcribing, Finishing, Refining };
QString transcribePhaseLabel(TranscribePhase phase);

// The steps the step indicator at the top of a Transcribe surface names:
// the setup form, the running batch and the results.
enum class TranscribeStep { Configure, Transcribe, Export };
QString transcribeStepLabel(TranscribeStep step);

// "3 min 7 s", or "42 s" under a minute.
QString durationLabel(qint64 ms);
// A setup list row's detail, "3 min 7 s · 3.6 MB"; the size alone while the
// length is unknown (negative).
QString audioFileDetail(qint64 bytes, qint64 durationMs);

// The model a refinement provider is set to use, or empty for none.
QString refinementModel(const QString &providerId, const RefinementSettings &settings);
// Whether a batch with these options refines its transcripts, which is when
// the results offer the raw text beside the refined one.
bool refinesTranscripts(const TranscribeOptions &options);
// The raw transcript when asked for or when no refined one came back.
QString shownTranscript(const TranscribeFileResult &result, bool raw);
// A result's second line: why it failed, or its length and word count.
QString resultMeta(const TranscribeFileResult &result, qint64 durationMs, bool raw);
// What Copy all puts on the clipboard: every finished transcript, each under a
// "# file name" heading when there are several.
QString allTranscripts(const QList<TranscribeFileResult> &results, bool raw);

// "Transcribing · memo.wav (2 of 3)"; current is -1 before the first file.
QString processingTitle(const QStringList &batch, int current);
enum class TranscribeQueueState { Waiting, Current, Done, Failed };
TranscribeQueueState queueState(int index, int current, const QList<TranscribeFileResult> &finished);
// The current file shows the phase it is in.
QString queueStateLabel(TranscribeQueueState state, const QString &phase);

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
// The line above a batch's results. batchSize counts every file the batch
// started with, so a cancelled batch can say how far it got.
QString batchSummary(const QList<TranscribeFileResult> &results,
                     int batchSize,
                     bool cancelled,
                     const QHash<QString, qint64> &durationsMs,
                     const TranscribeOptions &options,
                     const TranscribeBatchLabels &labels);

} // namespace speecher
