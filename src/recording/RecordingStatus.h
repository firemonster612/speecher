#pragma once

#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

namespace speecher {

// One audio source of a recording and the speaker its lines name.
struct RecordingStream {
    enum class State { Recording, Reconnecting, Stopped };

    QString speaker;
    State state = State::Recording;
    // Why the stream stopped or is reconnecting, or why its last line could
    // not be written.
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
};

// How status and its JSON name a stream's state.
QString recordingStreamStateName(RecordingStream::State state);

// How `record status --json` and the IPC response carry a status.
QJsonObject recordingStatusJson(const RecordingStatus &status);
RecordingStatus recordingStatusFromJson(const QJsonObject &object);

} // namespace speecher

Q_DECLARE_METATYPE(speecher::RecordingStatus)
