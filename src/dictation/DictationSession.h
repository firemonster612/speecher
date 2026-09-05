#pragma once

#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"
#include "dictation/StartupPreparationRunner.h"
#include "dictation/TranscriptPipeline.h"

#include <QMetaObject>
#include <QPointer>
#include <QVector>

#include <optional>

class QTimer;

namespace speecher {

class ProviderRegistry;
class SettingsStore;
class TranscriptState;

class DictationSession : public QObject {
    Q_OBJECT

public:
    DictationSession(SettingsStore *settings,
                     AudioInput *audio,
                     MediaController *mediaController,
                     TextDeliveryAdapter *delivery,
                     ProviderRegistry *providers,
                     QObject *parent = nullptr);
    DictationSession(SettingsStore *settings,
                     AudioInput *audio,
                     MediaController *mediaController,
                     TargetProvider *targetProvider,
                     TextDeliveryAdapter *delivery,
                     ProviderRegistry *providers,
                     QObject *parent = nullptr);
    ~DictationSession() override;

    DictationState state() const;
    QString stateName() const;
    QString lastMessage() const;
    SessionResponse response(bool ok = true, const QString &message = {}) const;
    void toggleWithFormat(OutputFormat format);
    void startListeningWithFormat(OutputFormat format);
    void setScreenshotContextProvider(ScreenshotContextProvider *provider);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    void popupPresented(quint64 generation);
    // How long startup waits for the popup to paint before proceeding without
    // it. Tests raise it so a slow runner cannot fire it under an assertion.
    static void setPopupPaintFallbackMs(int ms);

signals:
    void stateChanged(const QString &stateName);
    void statusChanged(const QString &status);
    void previewChanged(const QString &transcript);
    void transcriptDelivered(const QString &text);
    void previewDisplayChanged(const QString &preview);
    void audioLevelChanged(float level);
    void popupStatusChanged(const QString &status);
    void popupShowRequested(quint64 generation);
    void popupHideRequested();
    void popupFrozenChanged(bool frozen);
    void popupRefiningChanged(bool refining);
    void popupOAuthRefreshRequested();
    void popupListeningIndicatorRequested();
    void popupMessageRequested(const QString &message);
    void popupErrorRequested(const QString &message);

private:
    static int s_popupPaintFallbackMs;

    void setState(DictationState state, const QString &message = {});
    void continueStartupAfterPopup(quint64 generation);
    void finishStartupPreparation(const StartupPreparationResult &result);
    void continueStartupAfterPreparation(quint64 generation, const AppSettings &settings);
    void failStartup(quint64 generation, const QString &message);
    void beginRefinement(quint64 generation);
    void failSelectionEdit(const QString &message);
    void handleSpeechFailure(const SpeechFailure &failure);
    void deliverFinal(const QString &text);
    void clearScreenshotContext();
    void resumePausedMedia();
    bool selectSpeechTranscriber(const QString &providerId, QString *error);
    bool selectTranscriptRefiner(const QString &providerId, QString *error);
    void connectSpeechTranscriber(SpeechTranscriber *transcriber);
    void connectTranscriptRefiner(TranscriptRefiner *refiner);
    void toggleSession(std::optional<OutputFormat> format);
    void startSession(std::optional<OutputFormat> format);
    SettingsStore *m_settings = nullptr;
    AudioInput *m_audio = nullptr;
    MediaController *m_mediaController = nullptr;
    TargetProvider *m_targetProvider = nullptr;
    ScreenshotContextProvider *m_screenshotProvider = nullptr;
    TextDeliveryAdapter *m_delivery = nullptr;
    ProviderRegistry *m_providers = nullptr;
    TranscriptState *m_transcript = nullptr;
    StartupPreparationRunner *m_startupRunner = nullptr;
    QTimer *m_completionTimer = nullptr;
    // The registry owns these and may be destroyed first; never call a dead one.
    QPointer<SpeechTranscriber> m_transcriber;
    QPointer<TranscriptRefiner> m_refiner;
    QVector<QMetaObject::Connection> m_transcriberConnections;
    QVector<QMetaObject::Connection> m_refinerConnections;
    DictationState m_state = DictationState::Idle;
    QString m_lastMessage;
    QString m_refinedText;
    TranscriptPipelineResult m_transcriptPipeline;
    quint64 m_generation = 0;
    quint64 m_audioGeneration = 0;
    quint64 m_attemptId = 0;
    quint64 m_continuedStartupGeneration = 0;
    std::optional<AppSettings> m_sessionSettings;
    Target m_target;
    QByteArray m_screenshotData;
    QString m_screenshotMediaType;
    quint64 m_screenshotCaptureGeneration = 0;
    quint64 m_refinementGeneration = 0;
    bool m_heardSpeech = false;
    int m_speechReconnectsLeft = 0;
};

} // namespace speecher
