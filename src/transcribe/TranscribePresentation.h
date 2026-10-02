#pragma once

#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/Subtitles.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace speecher {

class ProviderRegistry;

// The wording of the Transcribe surfaces, shared so the three front ends say
// the same things. Only text lives here; each front end keeps its own widgets.

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

// How long a finished file holds at the end, its playhead or bar full,
// before the next file replaces it.
inline constexpr int kTranscribeLandingMs = 500;

// The steps the step indicator at the top of a Transcribe surface names:
// the setup form, the running batch and the results.
enum class TranscribeStep { Configure, Transcribe, Export };
QString transcribeStepLabel(TranscribeStep step);
// The line under the step indicator; empty for a step that needs none.
QString transcribeStepHint(TranscribeStep step);

// The fixed wording of the Transcribe surfaces: the window title, section
// titles, row labels and their help, button captions and dialog titles.
enum class TranscribeText {
    WindowTitle,
    AudioFilesSection,
    TranscriptionSection,
    RefinementSection,
    OutputSection,
    FilesDialogTitle,
    RemoveFile,
    Service,
    Vocabulary,
    VocabularyHelp,
    Refiner,
    RefinerHelp,
    NoRefiner,
    RefinerModel,
    Cleanup,
    CleanupHelp,
    WritingProfile,
    WritingProfileHelp,
    Tone,
    ToneHelp,
    SaveTranscripts,
    Folder,
    ChangeFolder,
    FolderDialogTitle,
    Cancel,
    PartialName,
    PartialPlaceholder,
    Refined,
    Raw,
    CopyAll,
    Copy,
    Copied,
    ExportAll,
    Export,
    ExportAllDialogTitle,
    ExportDialogTitle,
    TextFiles,
    Saved,
    Retry,
    Retrying,
    TranscribeMore,
    // The accessible name of the waveform progress meter.
    ProgressName,
    // Beside the disabled Transcribe button while no file is listed.
    NoFilesYet,
    // On the refinement rows while no provider is chosen.
    NeedsRefiner,
    // What a drag over the window says it will do.
    DropToAdd,
};
QString transcribeText(TranscribeText text);

// The button that opens the file chooser, before any file is listed and after.
QString chooseFilesCaption(bool anyListed);
// "Transcribe", or "Transcribe 3 files".
QString startCaption(int fileCount);
// The results section's title, singular for one transcript.
QString resultsTitle(int resultCount);
// A Save transcripts choice, and the help under the row while it is chosen.
QString destinationLabel(TranscriptDestination destination);
QString destinationHint(TranscriptDestination destination);
// "Could not save <path>: <reason>", for a transcript that failed to write.
QString transcriptSaveError(const QString &path, const QString &reason);
// "42%" for 0.42.
QString percentLabel(qreal progress);
// "42 percent" for 0.42: the progress meter's value, as a screen reader says it.
QString percentSpoken(qreal progress);

// Under the file chooser while no files are listed, and its tooltip naming
// every extension from transcribableExtensions().
QString mediaFilesHint();
QString mediaFilesTooltip();

// "3 min 7 s", or "42 s" under a minute.
QString durationLabel(qint64 ms);
// A setup list row's detail, "3 min 7 s · 3.6 MB"; the size alone while the
// length is unknown (negative).
QString audioFileDetail(qint64 bytes, qint64 durationMs);

// The model a refinement provider is set to use, or empty for none.
QString refinementModel(const QString &providerId, const RefinementSettings &settings);
// The help under that model on the Configure step: where it is set.
QString refinementModelHint();
// Whether a batch with these options refines its transcripts, which is when
// the results offer the raw text beside the refined one.
bool refinesTranscripts(const TranscribeOptions &options);
// The raw transcript when asked for or when no refined one came back.
QString shownTranscript(const TranscribeFileResult &result, bool raw);
// A result's second line: why it failed, or its length and word count.
QString resultMeta(const TranscribeFileResult &result, qint64 durationMs, bool raw);
// A choice in a transcript's Export menu, and its file type in the save dialog.
QString transcriptFormatCaption(TranscriptFormat format);
QString transcriptFormatFileType(TranscriptFormat format);
// Whether a finished transcript exports in that format: text always,
// subtitles only when the speech provider returned timings.
bool canExportAs(const TranscribeFileResult &result, TranscriptFormat format);
// What Export writes: the shown transcript as text. Subtitles always come
// from the Raw Transcript, which the timings belong to.
QString exportedTranscript(const TranscribeFileResult &result, TranscriptFormat format, bool raw);
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
// The line under the results summary that says where subtitles come from, or
// why there are none; empty when no transcript finished.
QString subtitlesNote(const QList<TranscribeFileResult> &results, const TranscribeBatchLabels &labels);

} // namespace speecher
