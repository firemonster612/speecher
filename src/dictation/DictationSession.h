#pragma once

#include "core/DictationRecord.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
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
    // A start that arrived while the microphone was stopping and waits for it.
    bool startPending() const;
    QString lastMessage() const;
    // Why the last session failed, kept after it goes back to idle so Home
    // can still say it; empty once the next session starts.
    QString lastFailure() const;
    QString lastTranscript() const;
    SessionResponse response(bool ok = true, const QString &message = {}) const;
    void toggleWith(const SessionOverrides &overrides);
    void startListeningWith(const SessionOverrides &overrides);
    void setScreenshotContextProvider(ScreenshotContextProvider *provider);
    // Whether the system says it can reach the internet. Only an outcome's
    // wording uses it: every provider in a chain is still tried.
    void setReachability(Reachability reachability);
    // The keys that keep and replace while a selection edit is reviewed, as
    // the review names them. The controller sets them whenever it takes or
    // lets go of keys; a review on screen is shown again with the new ones.
    void setReviewKeys(const ReviewKeys &keys);

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
    // While a selection edit is reviewed it keeps the original, silently.
    // Dismisses an error; does nothing while idle or delivering, when the text
    // is already out.
    void cancel();
    // Ends a review by putting the edit in place of the selection, or on the
    // clipboard when the selection is no longer the one it was made from.
    void replaceSelection();
    // Pause turns the microphone off and lets the speech provider finish the
    // words already spoken; they stay in the transcript. Resume listens on in
    // a fresh speech attempt. Stop while paused delivers what was said.
    void pause();
    void resume();
    // The pause button: pauses, or resumes while paused.
    void togglePause();
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
    // A delivery's receipt, which kind of outcome it reports, and the fix it
    // offers when a fallback provider stood in for one that needs attention
    // (an expired sign-in); ErrorFix::None otherwise.
    void popupMessageRequested(const QString &message,
                               PopupOutcome outcome,
                               const speecher::PopupErrorAction &fix = {});
    void popupErrorRequested(const QString &message, const speecher::PopupErrorAction &fix);
    // A selection edit to show against the selection until replaceSelection()
    // or cancel() ends the review.
    void popupSelectionEditReviewRequested(const speecher::SelectionEditReview &review);
    // What a session learned about a provider's sign-in: present when it
    // prepared, missing or turned down when it failed for that.
    void providerSignInObserved(const QString &providerId, bool signedIn);
    // The session holds the microphone from a start's request, before it
    // prepares, or a resume, until the microphone has stopped, post-roll and
    // all; not while paused. A warm microphone's pre-roll, from just before
    // the request, was heard by others before the session held it.
    void microphoneChanged(bool held);
    // The session listens from its first Listening until it stops listening,
    // through pauses; a start that never listened does not.
    void listeningChanged(bool listening);

private:
    static int s_popupPaintFallbackMs;
    static int s_stableAttemptMs;

    void setState(DictationState state, const QString &message = {}, const PopupErrorAction &fix = {});
    using Stage = ProviderAttemptIssue::Stage;
    void continueStartupAfterPopup(quint64 generation);
    bool selectProviders();
    void prepareSpeechProvider();
    void finishStartupPreparation(const StartupPreparationResult &result);
    void finishSpeechPreparation(const StartupPreparationResult &result);
    void speechProviderReady();
    void speechChainExhausted();
    void holdPendingAudio(const QByteArray &pcm);
    bool speechFallbackRemains(const SpeechFailure &failure) const;
    void switchSpeechProvider(const SpeechFailure &failure);
    void noteSpeechFailure(const SpeechFailure &failure);
    void retireSpeechAttempt();
    SpeechSettings speechSettingsFor(const QString &providerId) const;
    void noteProviderIssue(ProviderRole role, const QString &providerId, Stage stage, const ProviderFailure &failure);
    bool offlineFor(ProviderRole role, const QString &providerId) const;
    void noteSignIn(const QString &providerId, bool signedIn);
    void noteRan(ProviderRole role, const QString &providerId);
    ProviderLabels providerLabels() const;
    void continueStartupAfterPreparation(quint64 generation);
    void failStartup(const QString &message, const PopupErrorAction &fix);
    void beginRefinement(quint64 generation);
    void prepareRefiner();
    void finishRefinerPreparation(const StartupPreparationResult &result);
    void startRefiner();
    void enterRefining();
    RefinementSettings refinerSettings() const;
    void handleRefinementFailure(const ProviderFailure &failure, Stage stage);
    void retireRefiner();
    void deliverWithoutRefinement();
    void failSelectionEdit(const QString &message);
    void reviewSelectionEdit(const QString &revised);
    void emitReview();
    bool selectionUnchanged();
    void handleSpeechFailure(const SpeechFailure &failure);
    void endSpeechAfterFailure(const SpeechFailure &failure);
    void rollOverSpeechAttempt();
    void startNextAttempt();
    // Opens the attempt after a pause once the paused one has finished, and
    // sends it the audio heard in between.
    void resumeAttempt();
    // Sends the open attempt the audio that waited for one, in order.
    void sendPendingAudio();
    void refineAfterLastAttempt();
    void attemptEndedWhileStopping();
    // QtAudioInput::stop() spins a nested event loop for the post-roll, during
    // which commands and provider signals are dispatched. Callers settle the
    // session before stopping and recheck it afterwards.
    void stopAudio();
    bool startAudio(QString *error);
    void setMicrophoneOpen(bool open);
    void updateMicrophoneHeld();
    void updateListening();
    void refillReconnectsIfAttemptWasStable();
    bool attemptWasStable() const;
    // note follows the receipt, such as why an edit went to the clipboard.
    void deliverFinal(const QString &text, const QString &note = {});
    void discard();
    void clearScreenshotContext();
    void resumePausedMedia();
    bool selectSpeechTranscriber(const QString &providerId, QString *error);
    bool selectTranscriptRefiner(const QString &providerId, QString *error);
    void disconnectSpeechTranscriber();
    void connectSpeechTranscriber(SpeechTranscriber *transcriber);
    void disconnectTranscriptRefiner();
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
    // Numbers each preparation the runner is asked for; only the latest's
    // result counts.
    quint64 m_preparationRevision = 0;
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
    // The selection edit under review.
    QString m_reviewedEdit;
    ReviewKeys m_reviewKeys;
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
    // Audio no open attempt can take yet: heard after a resume while the
    // paused attempt still finishes, while the next provider prepares, or
    // after the last attempt ended during the stop. The next attempt gets
    // it, once.
    QList<QByteArray> m_pendingAudio;
    // The speech providers this session may use, primary first, and the
    // one it is on. It walks forward only.
    QStringList m_speechChain;
    int m_speechIndex = 0;
    // No provider is ready for the next attempt: the session is starting,
    // or the next provider prepares after one failed.
    bool m_awaitingSpeechProvider = false;
    // The failure that sent the session on to the next provider, which ends
    // the speech if none takes over.
    SpeechFailure m_switchFailure;
    // The refiners this session may use, primary first, and the one it is
    // on; each connection to a refiner gets its own revision.
    QStringList m_refinementChain;
    int m_refinementIndex = 0;
    quint64 m_refinementRevision = 0;
    // The raw transcript is delivered in place of a refinement that was
    // asked for.
    bool m_usedRawTranscript = false;
    // The refiner's sign-in renewed while starting: once a session, not for
    // each speech provider tried.
    bool m_refinerRefreshed = false;
    ProviderHistory m_providerHistory;
    Reachability m_reachability = Reachability::Unknown;
    // Inside stopAudio(); a session start waits in m_pendingStart until it
    // returns, unless a stop or cancel drops it first.
    int m_audioStopDepth = 0;
    bool m_microphoneOpen = false;
    bool m_microphoneHeld = false;
    bool m_listening = false;
    // The generation whose first microphone start is under way; it spins an
    // event loop too. A provider that becomes ready meanwhile opens only its
    // attempt, and that start goes on to Listening.
    quint64 m_microphoneStartGeneration = 0;
    std::optional<SessionOverrides> m_pendingStart;
    // The last attempt ended while the microphone was stopping; the post-roll
    // since waits in m_pendingAudio for stopListening().
    bool m_attemptEndedDuringStop = false;
};

} // namespace speecher
