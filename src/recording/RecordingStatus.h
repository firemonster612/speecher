#pragma once

#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

namespace speecher {

// The longest a recording's stop waits for the speech provider to finish
// the utterances left, three minutes: a few at the Custom Endpoint's
// slowest.
constexpr int kLongestRecordingStopMs = 180000;

// One audio source of a recording and the speaker its lines name.
struct RecordingStream {
    // Paused: the microphone's, while a dictation has it; it sends silence.
    enum class State { Recording, Paused, Reconnecting, Stopped };

    QString speaker;
    State state = State::Recording;
    // Why the stream stopped or is reconnecting.
    QString problem;
    // Audio dropped while the stream was down, oldest first, past what a
    // recording keeps for it.
    qint64 lostAudioMs = 0;
};

struct RecordingStatus {
    bool recording = false;
    QString path;
    qint64 durationMs = 0;
    QList<RecordingStream> streams;
    // Lines that could not be written to the file, and why the first could
    // not. Nothing clears them while the recording runs.
    int unwrittenLines = 0;
    QString writeError;
    // Why the microphone's audio keeps what the speakers play: Speecher's
    // echo canceller could not start, or the microphone the system was asked
    // to cancel echo in records without it. Empty when echo is cancelled or
    // nothing asked for it.
    QString echoCancellationWarning;
};

// How status and its JSON name a stream's state.
QString recordingStreamStateName(RecordingStream::State state);

// How `record status --json` and the IPC response carry a status.
QJsonObject recordingStatusJson(const RecordingStatus &status);
RecordingStatus recordingStatusFromJson(const QJsonObject &object);

} // namespace speecher

Q_DECLARE_METATYPE(speecher::RecordingStatus)
