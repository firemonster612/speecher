#pragma once

#include "core/BindingProcessor.h"
#include "dictation/DictationInterfaces.h"
#include "dictation/DictationTypes.h"

#include <QMetaObject>
#include <QVector>

#include <memory>
#include <optional>

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
    ~DictationSession() override;

    DictationState state() const;
    QString stateName() const;
    QString lastMessage() const;
    SessionResponse response(bool ok = true, const QString &message = {}) const;

public slots:
    void toggle();
    void startListening();
    void stopListening();

signals:
    void statusChanged(const QString &status);
    void previewChanged(const QString &transcript);
    void previewDisplayChanged(const QString &preview);
    void audioLevelChanged(float level);
    void popupStatusChanged(const QString &status);
    void popupShowRequested();
    void popupHideRequested();
    void popupHidePreviewRequested();
    void popupFrozenChanged(bool frozen);
    void popupRefiningChanged(bool refining);
    void popupOAuthRefreshRequested();
    void popupListeningIndicatorRequested();
    void popupMessageRequested(const QString &message);

private:
    struct StartupPreparation;

    void setState(DictationState state, const QString &message = {});
    void startPreparationWorker(quint64 generation,
                                const AppSettings &settings,
                                std::optional<SpeechPrepareJob> speechPrepareJob,
                                std::optional<RefinementRefreshJob> refinerRefreshJob,
                                const SpeechPrepareResult &speechPrepared);
    void finishStartupPreparation(quint64 generation,
                                  const AppSettings &settings,
                                  const std::shared_ptr<StartupPreparation> &preparation);
    void continueStartupAfterPreparation(quint64 generation, const AppSettings &settings);
    void failStartup(quint64 generation, const QString &message);
    void beginRefinement(quint64 generation);
    void deliverFinal(const QString &text);
    void resumePausedMedia();
    bool selectSpeechTranscriber(const QString &providerId, QString *error);
    bool selectTranscriptRefiner(const QString &providerId, QString *error);
    void connectSpeechTranscriber(SpeechTranscriber *transcriber);
    void connectTranscriptRefiner(TranscriptRefiner *refiner);

    SettingsStore *m_settings = nullptr;
    AudioInput *m_audio = nullptr;
    MediaController *m_mediaController = nullptr;
    TextDeliveryAdapter *m_delivery = nullptr;
    ProviderRegistry *m_providers = nullptr;
    TranscriptState *m_transcript = nullptr;
    SpeechTranscriber *m_transcriber = nullptr;
    TranscriptRefiner *m_refiner = nullptr;
    QVector<QMetaObject::Connection> m_transcriberConnections;
    QVector<QMetaObject::Connection> m_refinerConnections;
    DictationState m_state = DictationState::Idle;
    QString m_lastMessage;
    QString m_refinedText;
    BindingProcessingResult m_bindingResult;
    QList<BindingRule> m_activeBindingRules;
    QStringList m_noBindPhrases;
    bool m_allowPostRefinementBindings = true;
    quint64 m_generation = 0;
    std::shared_ptr<StartupPreparation> m_startupPreparation;
};

} // namespace speecher
