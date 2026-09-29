#include "transcribe/TranscribePresentation.h"

#include "core/settings/SettingsSchema.h"
#include "providers/ProviderRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

// String literals write non-ASCII characters as \u escapes. MSVC reads a
// source without a BOM in the system code page unless it is given /utf-8, and
// escapes come out the same either way.

namespace speecher {
namespace {

const QString kSeparator = QStringLiteral(" \u00b7 ");

int wordCount(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return int(text.split(whitespace, Qt::SkipEmptyParts).size());
}

QString providerLabel(const QList<ProviderDescriptor> &providers, const QString &id)
{
    const auto found = std::find_if(providers.cbegin(), providers.cend(),
                                    [&id](const ProviderDescriptor &provider) { return provider.id == id; });
    return found == providers.cend() ? id : found->label;
}

} // namespace

QString transcribePhaseLabel(TranscribePhase phase)
{
    switch (phase) {
    case TranscribePhase::Reading:
        return QStringLiteral("Reading the audio\u2026");
    case TranscribePhase::Transcribing:
        return QStringLiteral("Transcribing\u2026");
    case TranscribePhase::Finishing:
        return QStringLiteral("Finishing the transcript\u2026");
    case TranscribePhase::Refining:
        return QStringLiteral("Refining\u2026");
    }
    return {};
}

namespace {

struct ProgressSpan {
    qreal from;
    qreal to;
};

// Each phase's share of a file's progress bar. Sending takes most of it: it
// is the part that grows with the file's length, where finishing and
// refining take seconds whatever the length. Without refinement, sending and
// finishing stretch over refining's share. Every span ends below 1.
ProgressSpan progressSpan(TranscribePhase phase, bool refines)
{
    switch (phase) {
    case TranscribePhase::Reading:
        return {0.0, 0.05};
    case TranscribePhase::Transcribing:
        return {0.05, refines ? 0.75 : 0.90};
    case TranscribePhase::Finishing:
        return refines ? ProgressSpan{0.75, 0.80} : ProgressSpan{0.90, 0.97};
    case TranscribePhase::Refining:
        // Picks up where finishing stopped.
        return {progressSpan(TranscribePhase::Finishing, refines).to, 0.97};
    }
    return {0.0, 0.0};
}

// An open-ended wait covers about two thirds of its span in this time, and
// keeps slowing after that.
constexpr qreal kWaitEaseMs = 4000.0;

} // namespace

qreal overallFileProgress(qreal fractionSent, TranscribePhase phase, bool refines, qint64 msInPhase)
{
    const ProgressSpan span = progressSpan(phase, refines);
    const qreal within = phase == TranscribePhase::Transcribing
        ? std::clamp(fractionSent, 0.0, 1.0)
        : 1.0 - std::exp(-qreal(std::max<qint64>(msInPhase, 0)) / kWaitEaseMs);
    return span.from + (span.to - span.from) * within;
}

QString transcribeStepLabel(TranscribeStep step)
{
    switch (step) {
    case TranscribeStep::Configure:
        return QStringLiteral("Configure");
    case TranscribeStep::Transcribe:
        return QStringLiteral("Transcribe");
    case TranscribeStep::Export:
        return QStringLiteral("Export");
    }
    return {};
}

QString transcribeStepHint(TranscribeStep step)
{
    return step == TranscribeStep::Configure ? QStringLiteral("Check these options, then press Transcribe.")
                                             : QString();
}

QString transcribeText(TranscribeText text)
{
    switch (text) {
    case TranscribeText::WindowTitle:
        return QStringLiteral("Transcribe \u2014 Speecher");
    case TranscribeText::AudioFilesSection:
        return QStringLiteral("Audio files");
    case TranscribeText::TranscriptionSection:
        return QStringLiteral("Transcription");
    case TranscribeText::RefinementSection:
        return QStringLiteral("Refinement");
    case TranscribeText::OutputSection:
        return QStringLiteral("Output");
    case TranscribeText::FilesDialogTitle:
        return QStringLiteral("Choose audio files");
    case TranscribeText::RemoveFile:
        return QStringLiteral("Remove");
    case TranscribeText::Service:
        return QStringLiteral("Service");
    case TranscribeText::Vocabulary:
        return QStringLiteral("Apply vocabulary");
    case TranscribeText::VocabularyHelp:
        return QStringLiteral("Use your custom vocabulary and corrections on the result");
    case TranscribeText::Refiner:
        return QStringLiteral("Provider");
    case TranscribeText::RefinerHelp:
        return QStringLiteral("Clean up the raw transcripts with a language model");
    case TranscribeText::NoRefiner:
        return QStringLiteral("None");
    case TranscribeText::RefinerModel:
        return QStringLiteral("Model");
    case TranscribeText::Cleanup:
        return QStringLiteral("Cleanup");
    case TranscribeText::CleanupHelp:
        return QStringLiteral("How much the model may rewrite");
    case TranscribeText::WritingProfile:
        return QStringLiteral("Writing Profile");
    case TranscribeText::WritingProfileHelp:
        return QStringLiteral("Sets cleanup and tone; you can still adjust them here");
    case TranscribeText::Tone:
        return QStringLiteral("Tone");
    case TranscribeText::ToneHelp:
        return QStringLiteral("Optional override on top of the profile");
    case TranscribeText::SaveTranscripts:
        return QStringLiteral("Save transcripts");
    case TranscribeText::Folder:
        return QStringLiteral("Folder");
    case TranscribeText::ChangeFolder:
        return QStringLiteral("Change\u2026");
    case TranscribeText::FolderDialogTitle:
        return QStringLiteral("Save transcripts in");
    case TranscribeText::Cancel:
        return QStringLiteral("Cancel");
    case TranscribeText::PartialName:
        return QStringLiteral("Transcript so far");
    case TranscribeText::PartialPlaceholder:
        return QStringLiteral("The transcript appears here as it is heard.");
    case TranscribeText::Refined:
        return QStringLiteral("Refined");
    case TranscribeText::Raw:
        return QStringLiteral("Raw");
    case TranscribeText::CopyAll:
        return QStringLiteral("Copy all");
    case TranscribeText::Copy:
        return QStringLiteral("Copy");
    case TranscribeText::Copied:
        return QStringLiteral("Copied");
    case TranscribeText::ExportAll:
        return QStringLiteral("Export all\u2026");
    case TranscribeText::Export:
        return QStringLiteral("Export\u2026");
    case TranscribeText::ExportAllDialogTitle:
        return QStringLiteral("Export transcripts to");
    case TranscribeText::ExportDialogTitle:
        return QStringLiteral("Export transcript");
    case TranscribeText::TextFiles:
        return QStringLiteral("Text files");
    case TranscribeText::Saved:
        return QStringLiteral("Saved");
    case TranscribeText::Retry:
        return QStringLiteral("Retry");
    case TranscribeText::Retrying:
        return QStringLiteral("Retrying\u2026");
    case TranscribeText::TranscribeMore:
        return QStringLiteral("Transcribe more files");
    case TranscribeText::ProgressName:
        return QStringLiteral("Transcription progress");
    }
    return {};
}

QString chooseFilesCaption(bool anyListed)
{
    return anyListed ? QStringLiteral("Add more files\u2026") : QStringLiteral("Choose audio files\u2026");
}

QString startCaption(int fileCount)
{
    return fileCount > 1 ? QStringLiteral("Transcribe %1 files").arg(fileCount) : QStringLiteral("Transcribe");
}

QString resultsTitle(int resultCount)
{
    return resultCount > 1 ? QStringLiteral("Transcripts") : QStringLiteral("Transcript");
}

QString destinationLabel(TranscriptDestination destination)
{
    switch (destination) {
    case TranscriptDestination::BesideInput:
        return QStringLiteral("Next to each audio file");
    case TranscriptDestination::Folder:
        return QStringLiteral("One folder\u2026");
    case TranscriptDestination::None:
        return QStringLiteral("Just show them here");
    }
    return {};
}

QString destinationHint(TranscriptDestination destination)
{
    return destination == TranscriptDestination::None
        ? QStringLiteral("Copy or export from the results afterwards")
        : QStringLiteral("Each transcript is saved as \u27e8name\u27e9-transcribed.txt");
}

QString transcriptSaveError(const QString &path, const QString &reason)
{
    return QStringLiteral("Could not save %1: %2").arg(QDir::toNativeSeparators(path), reason);
}

QString percentLabel(qreal progress)
{
    return QStringLiteral("%1%").arg(int(progress * 100));
}

QString percentSpoken(qreal progress)
{
    return QStringLiteral("%1 percent").arg(int(progress * 100));
}

QString mediaFilesHint()
{
    return QStringLiteral("Drop audio and video files here or choose them") + kSeparator
        + transcribableExtensions().join(QStringLiteral(", "));
}

QString refinementModelHint()
{
    return QStringLiteral("Change it on the %1 page").arg(paneTitleForRow(QStringLiteral("openAiModel")));
}

QString durationLabel(qint64 ms)
{
    const qint64 seconds = (ms + 500) / 1000;
    return seconds >= 60 ? QStringLiteral("%1 min %2 s").arg(seconds / 60).arg(seconds % 60)
                         : QStringLiteral("%1 s").arg(seconds);
}

QString audioFileDetail(qint64 bytes, qint64 durationMs)
{
    const QString size = QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
    return durationMs >= 0 ? durationLabel(durationMs) + kSeparator + size : size;
}

QString refinementModel(const QString &providerId, const RefinementSettings &settings)
{
    if (providerId == QStringLiteral("openai")) {
        return settings.openAiModel;
    }
    if (providerId == QStringLiteral("anthropic")) {
        return settings.anthropicModel;
    }
    return {};
}

bool refinesTranscripts(const TranscribeOptions &options)
{
    return options.refinementProviderId != QStringLiteral("none")
        && options.cleanupStrength != QStringLiteral("none");
}

QString shownTranscript(const TranscribeFileResult &result, bool raw)
{
    return raw || result.refined.isEmpty() ? result.raw : result.refined;
}

QString resultMeta(const TranscribeFileResult &result, qint64 durationMs, bool raw)
{
    if (result.failed()) {
        return result.error;
    }
    const QString words = QStringLiteral("%1 words").arg(wordCount(shownTranscript(result, raw)));
    return durationMs >= 0 ? durationLabel(durationMs) + kSeparator + words : words;
}

QString allTranscripts(const QList<TranscribeFileResult> &results, bool raw)
{
    QStringList parts;
    for (const TranscribeFileResult &result : results) {
        if (result.failed()) {
            continue;
        }
        parts << (results.size() > 1
                      ? QStringLiteral("# %1\n\n%2").arg(QFileInfo(result.path).fileName(),
                                                         shownTranscript(result, raw))
                      : shownTranscript(result, raw));
    }
    return parts.join(QStringLiteral("\n\n\n"));
}

QString processingTitle(const QStringList &batch, int current)
{
    if (current < 0 || current >= batch.size()) {
        return QStringLiteral("Transcribing");
    }
    const QString name = QFileInfo(batch.at(current)).fileName();
    return batch.size() > 1
        ? QStringLiteral("Transcribing \u00b7 %1 (%2 of %3)").arg(name).arg(current + 1).arg(batch.size())
        : QStringLiteral("Transcribing \u00b7 %1").arg(name);
}

TranscribeQueueState queueState(int index, int current, const QList<TranscribeFileResult> &finished)
{
    if (index < finished.size()) {
        return finished.at(index).failed() ? TranscribeQueueState::Failed : TranscribeQueueState::Done;
    }
    return index == current ? TranscribeQueueState::Current : TranscribeQueueState::Waiting;
}

QString queueStateLabel(TranscribeQueueState state, const QString &phase)
{
    switch (state) {
    case TranscribeQueueState::Waiting:
        return QStringLiteral("Waiting");
    case TranscribeQueueState::Current:
        return phase;
    case TranscribeQueueState::Done:
        return QStringLiteral("Done");
    case TranscribeQueueState::Failed:
        return QStringLiteral("Failed");
    }
    return {};
}

TranscribeBatchLabels batchLabels(const TranscribeOptions &options,
                                  const ProviderRegistry &providers,
                                  const RefinementSettings &settings)
{
    TranscribeBatchLabels labels;
    labels.speech = providerLabel(providers.speechProviders(), options.speechProviderId);
    if (refinesTranscripts(options)) {
        labels.refinement = QStringLiteral("%1 %2")
                                .arg(providerLabel(providers.refinementProviders(), options.refinementProviderId),
                                     refinementModel(options.refinementProviderId, settings))
                                .trimmed();
    }
    return labels;
}

QString batchSummary(const QList<TranscribeFileResult> &results,
                     int batchSize,
                     bool cancelled,
                     const QHash<QString, qint64> &durationsMs,
                     const TranscribeOptions &options,
                     const TranscribeBatchLabels &labels)
{
    const int failed = int(std::count_if(results.cbegin(), results.cend(),
                                         [](const TranscribeFileResult &result) { return result.failed(); }));
    const int transcribed = int(results.size()) - failed;
    qint64 totalMs = 0;
    bool saved = false;
    for (const TranscribeFileResult &result : results) {
        totalMs += durationsMs.value(result.path, 0);
        saved = saved || !result.savedPath.isEmpty();
    }
    QStringList parts;
    if (cancelled) {
        parts << QStringLiteral("Canceled \u2014 %1 of %2 files transcribed").arg(transcribed).arg(batchSize);
    } else if (results.size() > 1) {
        parts << QStringLiteral("%1 transcripts").arg(transcribed);
    }
    if (failed > 0) {
        parts << QStringLiteral("%1 failed").arg(failed);
    }
    if (totalMs > 0) {
        parts << QStringLiteral("%1 of audio").arg(durationLabel(totalMs));
    }
    parts << QStringLiteral("transcribed with %1").arg(labels.speech);
    if (!labels.refinement.isEmpty()) {
        parts << QStringLiteral("refined with %1").arg(labels.refinement);
    }
    if (saved) {
        parts << (options.destination == TranscriptDestination::Folder
                      ? QStringLiteral("saved to %1").arg(QDir::toNativeSeparators(options.folder))
                      : QStringLiteral("saved next to each audio file"));
    }
    return parts.join(kSeparator);
}

} // namespace speecher
