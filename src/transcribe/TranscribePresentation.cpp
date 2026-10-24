#include "transcribe/TranscribePresentation.h"

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

QString transcriptSaveError(const QString &path, const QString &reason)
{
    return QStringLiteral("Could not save %1: %2").arg(QDir::toNativeSeparators(path), reason);
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

bool refinesTranscripts(const TranscribeOptions &options, const RefinementSettings &settings)
{
    const WritingProfileSettings profile =
        writingProfileSettingsFor(settings.writingProfiles, writingProfileFromName(options.writingProfile));
    return options.refinementProviderId != QStringLiteral("none")
        && refinedCleanupLevel(options.cleanupStrength, profile.outputLanguage) != QStringLiteral("none");
}

QString shownTranscript(const TranscribeFileResult &result, bool raw)
{
    return raw || result.refined.isEmpty() ? result.raw : result.refined;
}

bool canExportAs(const TranscribeFileResult &result, TranscriptFormat format)
{
    return !result.failed() && (format == TranscriptFormat::Text || !result.segments.isEmpty());
}

QString exportedTranscript(const TranscribeFileResult &result, TranscriptFormat format, bool raw)
{
    return format == TranscriptFormat::Text ? shownTranscript(result, raw) : subtitleFile(result.segments, format);
}

QString allTranscripts(const QList<TranscribeFileResult> &results, bool raw)
{
    QStringList parts;
    for (const TranscribeFileResult &result : results) {
        if (result.failed()) {
            continue;
        }
        parts << (results.size() > 1 ? QStringLiteral("# %1\n\n%2").arg(QFileInfo(result.path).fileName(),
                                                                           shownTranscript(result, raw))
                                     : shownTranscript(result, raw));
    }
    return parts.join(QStringLiteral("\n\n\n"));
}

QString subtitlesNeedTimings(const QString &speechProvider)
{
    return QStringLiteral("Subtitles need timings, and %1 returned none.").arg(speechProvider);
}

TranscribeBatchLabels batchLabels(const TranscribeOptions &options,
                                  const ProviderRegistry &providers,
                                  const RefinementSettings &settings)
{
    TranscribeBatchLabels labels;
    labels.speech = providerLabel(providers.speechProviders(), options.speechProviderId);
    if (refinesTranscripts(options, settings)) {
        labels.refinement = QStringLiteral("%1 %2")
                                .arg(providerLabel(providers.refinementProviders(), options.refinementProviderId),
                                     refinementModel(options.refinementProviderId, settings))
                                .trimmed();
    }
    return labels;
}

} // namespace speecher
