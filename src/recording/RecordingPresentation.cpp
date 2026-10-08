#include "recording/RecordingPresentation.h"

#include "dictation/DictationTypes.h"
#include "recording/RecordingStatus.h"

#include <QStringList>

namespace speecher {
namespace {

// People name a stream by its source; its speaker belongs to the file's lines.
// Only the microphone records so far.
const QString kMicrophoneSource = QStringLiteral("microphone");

} // namespace

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
        QStringList parts{recordingStreamStateName(stream.state)};
        if (!stream.problem.isEmpty()) {
            parts << stream.problem;
        }
        if (stream.lostAudioMs > 0) {
            parts << QStringLiteral("lost %1 of audio").arg(recordingClock(stream.lostAudioMs));
        }
        lines << QStringLiteral("%1: %2").arg(kMicrophoneSource, parts.join(QStringLiteral(", ")));
    }
    return lines.join(QLatin1Char('\n'));
}

QString recordingStreamProblemText(const RecordingStream &stream)
{
    QStringList problems;
    if (!stream.problem.isEmpty()) {
        switch (stream.state) {
        case RecordingStream::State::Recording:
            problems << QStringLiteral("The %1 stream has a problem: %2").arg(kMicrophoneSource, stream.problem);
            break;
        case RecordingStream::State::Reconnecting:
            problems << QStringLiteral("The %1 stream is reconnecting: %2").arg(kMicrophoneSource, stream.problem);
            break;
        case RecordingStream::State::Stopped:
            problems << QStringLiteral("The %1 stream stopped: %2").arg(kMicrophoneSource, stream.problem);
            break;
        }
    }
    if (stream.lostAudioMs > 0) {
        problems << QStringLiteral("The %1 stream was down so long that the oldest %2 of audio waiting for it was "
                                   "dropped.")
                        .arg(kMicrophoneSource, recordingClock(stream.lostAudioMs));
    }
    return problems.join(QLatin1Char('\n'));
}

QString recordingWriteProblemText(const RecordingStatus &status)
{
    if (status.unwrittenLines == 0) {
        return {};
    }
    return QStringLiteral("%1 (%2 %3 missing)")
        .arg(status.writeError)
        .arg(status.unwrittenLines)
        .arg(status.unwrittenLines == 1 ? QStringLiteral("line") : QStringLiteral("lines"));
}

QString statusWatchText(const QString &dictationState, const RecordingStatus &recording)
{
    if (!recording.recording) {
        return dictationState;
    }
    QStringList parts;
    if (dictationState != dictationStateName(DictationState::Idle)) {
        parts << dictationState;
    }
    parts << QStringLiteral("recording %1").arg(recordingClock(recording.durationMs));
    for (const RecordingStream &stream : recording.streams) {
        if (stream.state != RecordingStream::State::Recording) {
            parts << QStringLiteral("%1 %2").arg(kMicrophoneSource, recordingStreamStateName(stream.state));
        }
    }
    return parts.join(QStringLiteral(", "));
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

QString recordingSpeechProviderRefusal(const QString &providerLabel)
{
    return QStringLiteral("Recording needs Claude Voice, ChatGPT Codex or the Custom Endpoint. %1 can't record "
                          "yet.")
        .arg(providerLabel);
}

QString recordingStreamEndedText()
{
    return QStringLiteral("The speech stream ended.");
}

QString recordingUtteranceFailedText(const QString &reason)
{
    return QStringLiteral("An utterance could not be transcribed: %1").arg(reason);
}

QString recordingStopTimedOutText()
{
    return QStringLiteral("The speech provider did not finish in time, so the end of the recording is missing.");
}

QString recordingFileError(const QString &path, const QString &reason)
{
    return QStringLiteral("Could not write %1: %2").arg(path, reason);
}

} // namespace speecher
