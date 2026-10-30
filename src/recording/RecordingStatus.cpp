#include "recording/RecordingStatus.h"

#include <QJsonArray>

namespace speecher {

QString recordingStreamStateName(RecordingStreamStatus::State state)
{
    switch (state) {
    case RecordingStreamStatus::State::Recording:
        return QStringLiteral("recording");
    case RecordingStreamStatus::State::Paused:
        return QStringLiteral("paused");
    case RecordingStreamStatus::State::Reconnecting:
        return QStringLiteral("reconnecting");
    case RecordingStreamStatus::State::Stopped:
        break;
    }
    return QStringLiteral("stopped");
}

QJsonObject recordingStatusJson(const RecordingStatus &status)
{
    if (!status.recording) {
        return {{QStringLiteral("recording"), false}};
    }
    QJsonArray streams;
    for (const RecordingStreamStatus &stream : status.streams) {
        QJsonObject object{{QStringLiteral("speaker"), stream.speaker},
                           {QStringLiteral("state"), recordingStreamStateName(stream.state)}};
        if (!stream.problem.isEmpty()) {
            object.insert(QStringLiteral("problem"), stream.problem);
        }
        if (stream.lostAudioMs > 0) {
            object.insert(QStringLiteral("lostAudioMs"), stream.lostAudioMs);
        }
        streams.append(object);
    }
    QJsonObject object{{QStringLiteral("recording"), true},
                       {QStringLiteral("path"), status.path},
                       {QStringLiteral("durationMs"), status.durationMs},
                       {QStringLiteral("streams"), streams}};
    if (status.unwrittenLines > 0) {
        object.insert(QStringLiteral("unwrittenLines"), status.unwrittenLines);
        object.insert(QStringLiteral("writeError"), status.writeError);
    }
    if (!status.echoCancellationWarning.isEmpty()) {
        object.insert(QStringLiteral("echoCancellationWarning"), status.echoCancellationWarning);
    }
    return object;
}

RecordingStatus recordingStatusFromJson(const QJsonObject &object)
{
    RecordingStatus status;
    status.recording = object.value(QStringLiteral("recording")).toBool();
    status.path = object.value(QStringLiteral("path")).toString();
    status.durationMs = object.value(QStringLiteral("durationMs")).toInteger();
    for (const QJsonValue &value : object.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject stream = value.toObject();
        const QString state = stream.value(QStringLiteral("state")).toString();
        RecordingStreamStatus read{stream.value(QStringLiteral("speaker")).toString()};
        for (const RecordingStreamStatus::State known : {RecordingStreamStatus::State::Recording,
                                                         RecordingStreamStatus::State::Paused,
                                                         RecordingStreamStatus::State::Reconnecting,
                                                         RecordingStreamStatus::State::Stopped}) {
            if (state == recordingStreamStateName(known)) {
                read.state = known;
            }
        }
        read.problem = stream.value(QStringLiteral("problem")).toString();
        read.lostAudioMs = stream.value(QStringLiteral("lostAudioMs")).toInteger();
        status.streams.append(read);
    }
    status.unwrittenLines = object.value(QStringLiteral("unwrittenLines")).toInt();
    status.writeError = object.value(QStringLiteral("writeError")).toString();
    status.echoCancellationWarning = object.value(QStringLiteral("echoCancellationWarning")).toString();
    return status;
}

} // namespace speecher
