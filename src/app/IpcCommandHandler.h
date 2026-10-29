#pragma once

#include "app/SingleInstanceIpc.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <memory>

class QLocalSocket;

namespace speecher {

class ApplicationController;
class RecordingSession;

// Serves the daemon's local socket: answers each command line request from
// the controller, keeps `status --watch` clients current, and holds record
// stop's replies until the last utterance is written.
class IpcCommandHandler : public QObject {
    Q_OBJECT

public:
    IpcCommandHandler(ApplicationController &controller,
                      RecordingSession &recording,
                      std::shared_ptr<const PlatformComposition> platform,
                      QObject *parent = nullptr);

    IpcListenResult listen(QString *error = nullptr);
    // Serves request and answers it on socket, which may be null.
    void handle(const IpcRequest &request, QLocalSocket *socket);

private:
    IpcResponse response(bool ok = true, const QString &message = {}) const;
    // status's answer and each line status --watch is written: the dictation
    // state and the recording.
    IpcResponse statusResponse() const;
    void startRecording(const IpcRequest &request, QLocalSocket *socket);

    ApplicationController &m_controller;
    RecordingSession &m_recording;
    SingleInstanceIpc *m_ipc = nullptr;
    // record stop's clients, answered once the last utterance is written.
    QList<QPointer<QLocalSocket>> m_recordStopReplies;
};

} // namespace speecher
