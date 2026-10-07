#include "recording/RecordingPresentation.h"

#include "recording/RecordingSession.h"

#include <QStringList>

namespace speecher {

QString recordingClock(qint64 elapsedMs)
{
    const qint64 seconds = elapsedMs / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString recordingConsentNotice()
{
    return QStringLiteral("Recording other people may require their consent, depending on where you and they "
                          "are. Speecher shows this notice once.");
}

QString recordingStatusText(const RecordingStatus &status)
{
    if (!status.recording) {
        return notRecordingText();
    }
    QStringList lines{QStringLiteral("path: %1").arg(status.path),
                      QStringLiteral("duration: %1").arg(recordingClock(status.durationMs))};
    for (const RecordingStream &stream : status.streams) {
        const QString state = stream.running ? QStringLiteral("recording") : QStringLiteral("stopped");
        lines << (stream.problem.isEmpty() ? QStringLiteral("%1: microphone, %2").arg(stream.speaker, state)
                                           : QStringLiteral("%1: microphone, %2, %3")
                                                 .arg(stream.speaker, state, stream.problem));
    }
    return lines.join(QLatin1Char('\n'));
}

QString recordingStreamProblemText(const RecordingStream &stream)
{
    if (stream.problem.isEmpty()) {
        return {};
    }
    return stream.running ? QStringLiteral("The %1 stream has a problem: %2").arg(stream.speaker, stream.problem)
                          : QStringLiteral("The %1 stream stopped: %2").arg(stream.speaker, stream.problem);
}

QString notRecordingText()
{
    return QStringLiteral("not recording");
}

QString alreadyRecordingText(const QString &path)
{
    return QStringLiteral("Already recording to %1. Stop it with speecher record stop first.").arg(path);
}

QString recordingNeedsSetupText()
{
    return QStringLiteral("Finish setting up Speecher, then start the recording.");
}

QString batchSpeechProviderRefusal(const QString &providerLabel)
{
    return QStringLiteral("Recording needs a speech provider that streams text as it hears it, such as Claude Voice "
                          "or ChatGPT Codex. %1 does not.")
        .arg(providerLabel);
}

QString recordingStreamEndedText()
{
    return QStringLiteral("The speech stream ended.");
}

QString recordingFileError(const QString &path, const QString &reason)
{
    return QStringLiteral("Could not write %1: %2").arg(path, reason);
}

} // namespace speecher
