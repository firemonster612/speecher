#pragma once

#include "core/DictationRecord.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"
#include "dictation/PopupPresentation.h"
#include "dictation/StartupPreparationRunner.h"
#include "dictation/TranscriptPipeline.h"

#include <QElapsedTimer>
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
    // Why the last session failed, kept after it goes back to idle so Home
    // can still say it; empty once the next session starts.
    QString lastFailure() const;
    QString lastTranscript() const;
    SessionResponse response(bool ok = true, const QString &message = {}) const;
    void toggleWith(const SessionOverrides &overrides);
    void startListeningWith(const SessionOverrides &overrides);
    void setScreenshotContextProvider(ScreenshotContextProvider *provider);

public slots:
    void toggle();
    void startListening();
    void stopListening();
    // Shutdown teardown: cancels everything in flight and resumes paused
    // media without ever delivering text — quitting mid-dictation must not
    // paste into whatever window has focus.
    void cancelForShutdown();
    // Throws the session away from Starting through Refining: nothing is
    // pasted, copied or recorded, and the popup says "Canceled" for a moment.
    // Dismisses an error; does nothing while idle or delivering, when the text
    // is already out.
    void cancel();
    // Pause turns the microphone off and lets the speech provider finish the
    // words already spoken; they stay in the transcript. Resume listens on in
    // a fresh speech attempt. Stop while paused delivers what was said.
    void pause();
    void resume();
    void popupPresented(quint64 generation);
    // How long startup waits for the popup to paint before proceeding without
    // it. Tests raise it so a slow runner cannot fire it under an assertion.
    static void setPopupPaintFallbackMs(int ms);
    // How long a speech attempt must stream before its end counts as a
    // routine rollover rather than a failure. Tests lower it.
    static int stableAttemptMs();
    static void setStableAttemptMs(int ms);

signals:
    void stateChanged(const QString &stateName);
    void statusChanged(const QString &status);
    void previewChanged(const QString &transcript);
    void transcriptDelivered(const QString &text);
    // A delivered Dictation Session while insights are on; the controller
    // owns the log it goes into.
    void dictationRecorded(const DictationRecord &record);
    void previewDisplayChanged(const QString &preview);
    void audioLevelChanged(float level);
    void popupStatusChanged(const QString &status);
    void popupShowRequested(quint64 generation);
    void popupHideRequested();
    void popupFrozenChanged(bool frozen);
    void popupRefiningChanged(bool refining);
    // Last words of the refinement stream so far, for the popup preview while
    // Refining; the speech preview keeps flowing through previewDisplayChanged.
    void popupRefinementPreviewChanged(const QString &preview);
    void popupOAuthRefreshRequested();
    void popupListeningIndicatorRequested();
    // A delivery's receipt, and which kind of outcome it reports.
    void popupMessageRequested(const QString &message, PopupOutcome outcome);
    void popupErrorRequested(const QString &message, const speecher::PopupErrorAction &fix);

private:
    static int s_popupPaintFallbackMs;
    static int s_stableAttemptMs;

    void setState(DictationState state, const QString &message = {}, const PopupErrorAction &fix = {});
    void continueStartupAfterPopup(quint64 generation);
    void prepareProviders(quint64 generation);
    void finishStartupPreparation(const StartupPreparationResult &result);
    void continueStartupAfterPreparation(quint64 generation, const AppSettings &settings);
    void failStartup(quint64 generation, const QString &message);
    void beginRefinement(quint64 generation);
    void failSelectionEdit(const QString &message);
    void handleSpeechFailure(const SpeechFailure &failure);
    void rollOverSpeechAttempt();
    void startNextAttempt();
    // Opens the attempt after a pause once the paused one has finished, and
    // sends it the audio heard in between.
    void resumeAttempt();
    void refillReconnectsIfAttemptWasStable();
    bool attemptWasStable() const;
    void deliverFinal(const QString &text);
    void discard();
    void clearScreenshotContext();
    void resumePausedMedia();
    bool selectSpeechTranscriber(const QString &providerId, QString *error);
    bool selectTranscriptRefiner(const QString &providerId, QString *error);
    void connectSpeechTranscriber(SpeechTranscriber *transcriber);
    void connectTranscriptRefiner(TranscriptRefiner *refiner);
    void toggleSession(const SessionOverrides &overrides);
    void startSession(const SessionOverrides &overrides);
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
    QString m_lastFailure;
    QString m_speechWarning;
    QString m_lastTranscript;
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
    QString m_refinementStream;
    bool m_heardSpeech = false;
    int m_speechReconnectsLeft = 0;
    QElapsedTimer m_attemptClock;
    // From Listening to Stopping: the audio a Dictation Session transcribed.
    QElapsedTimer m_listeningClock;
    int m_listeningMs = 0;
    // Committed text carried over from before the current speech attempt; a
    // whole-attempt transcript replaces only what followed it.
    QString m_attemptBaseText;
    // The current attempt is finishing because of a pause. Its end is not a
    // rollover, and the next attempt may only open once it has come.
    bool m_finishingPausedAttempt = false;
    // Audio heard after a resume while the paused attempt still finishes.
    QList<QByteArray> m_resumeAudio;
};

} // namespace speecher
