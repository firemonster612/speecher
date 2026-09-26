#include "transcribe/TranscribePresentation.h"

#include "providers/ProviderRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

// Non-ASCII text is written as \u escapes: this file is also compiled by
// MSVC, which reads a source without a BOM in the system code page.

namespace speecher {
namespace {

const QString kSeparator = QStringLiteral(" · ");

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
        return QStringLiteral("Reading the audio…");
    case TranscribePhase::Transcribing:
        return QStringLiteral("Transcribing…");
    case TranscribePhase::Finishing:
        return QStringLiteral("Finishing the transcript…");
    case TranscribePhase::Refining:
        return QStringLiteral("Refining…");
    }
    return {};
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
        ? QStringLiteral("Transcribing · %1 (%2 of %3)").arg(name).arg(current + 1).arg(batch.size())
        : QStringLiteral("Transcribing · %1").arg(name);
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
        parts << QStringLiteral("Cancelled — %1 of %2 files transcribed").arg(transcribed).arg(batchSize);
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
