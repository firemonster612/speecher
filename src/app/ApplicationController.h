#pragma once

#include <QObject>
#include <QTimer>

#include "app/SingleInstanceIpc.h"

class QLocalSocket;

namespace speecher {

class AudioCapture;
class ClaudeVoiceClient;
class MainWindow;
class MediaPauseController;
class OpenAiRefiner;
class SecretStore;
class SettingsStore;
class TextDelivery;
class TranscriptState;
class TranscriberPopup;

class ApplicationController : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Starting, Listening, Stopping, Refining, Delivering, Error };
    Q_ENUM(State)

    explicit ApplicationController(bool popupOnly, QObject *parent = nullptr);

    SettingsStore *settings() const;
    SecretStore *secretStore() const;
    QString stateName() const;
    IpcResponse response(bool ok = true, const QString &message = {}) const;

    void showMainWindow();
    bool startIpc(QString *error = nullptr);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    void showMain();
    void handleIpcCommand(const QString &command, QLocalSocket *socket);

signals:
    void statusChanged(const QString &status);
    void previewChanged(const QString &preview);

private:
    void setState(State state, const QString &message = {});
    void beginRefinement();
    void deliverFinal(const QString &text);
    void resumePausedMedia();
    QUrl claudeVoiceUrl() const;

    bool m_popupOnly = false;
    State m_state = State::Idle;
    QString m_lastMessage;
    SettingsStore *m_settings = nullptr;
    SecretStore *m_secrets = nullptr;
    TranscriptState *m_transcript = nullptr;
    AudioCapture *m_audio = nullptr;
    MediaPauseController *m_mediaPause = nullptr;
    ClaudeVoiceClient *m_claude = nullptr;
    OpenAiRefiner *m_refiner = nullptr;
    TextDelivery *m_delivery = nullptr;
    TranscriberPopup *m_popup = nullptr;
    MainWindow *m_mainWindow = nullptr;
    SingleInstanceIpc *m_ipc = nullptr;
    QString m_refinedText;
};

} // namespace speecher
