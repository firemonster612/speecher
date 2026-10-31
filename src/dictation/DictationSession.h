#pragma once

#include "core/DictationRecord.h"
#include "core/ProviderReadiness.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationSpeech.h"
#include "dictation/DictationTypes.h"
#include "dictation/EditReview.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "dictation/TranscriptPipeline.h"
#include "providers/TranscriptRefiner.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QVector>

#include <optional>

class QTimer;

namespace speecher {

class ProviderRegistry;
class RefinementChain;
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
    // The state stateChanged and the status line report: Starting while a
    // start waits for the microphone to stop, otherwise state().
    DictationState reportedState() const;
    // reportedState()'s name, as IPC and the command line carry it.
    QString stateName() const;
    // A start that arrived while the microphone was stopping and waits for it.
    bool startPending() const;
    QString lastMessage() const;
    QString lastTranscript() const;
    // How long this session has listened so far, pauses left out.
    int listeningMs() const;
    // A selection edit is under review, a follow-up to it dictated or not.
    bool reviewUp() const;
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
    // While an edit is reviewed, toggle() and startListening() dictate a
    // follow-up instruction, which edits the revision under review;
    // cancelled or failed, it leaves the review as it was.
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

signals:
    void stateChanged(DictationState state);
    void statusChanged(const QString &status);
    void previewChanged(const QString &transcript);
    void transcriptDelivered(const QString &text);
    // A delivered Dictation Session while insights are on; the controller
    // owns the log it goes into.
    void dictationRecorded(const DictationRecord &record);
    void previewDisplayChanged(const QString &preview);
    void audioLevelChanged(float level);
    void popupShowRequested(quint64 generation);
    void popupHideRequested();
    // Last words of the refinement stream so far, for the popup preview while
    // Refining; the speech preview keeps flowing through previewDisplayChanged.
    void popupRefinementPreviewChanged(const QString &preview);
    // A provider's sign-in renews before the dictation can go on.
    void popupOAuthRefreshRequested();
    // A delivery's receipt, which kind of outcome it reports, and the fix it
    // offers when a fallback provider stood in for one that needs attention
    // (an expired sign-in); ErrorFix::None otherwise.
    void popupMessageRequested(const QString &message,
                               PopupOutcome outcome,
                               const speecher::PopupErrorAction &fix = {});
    void popupErrorRequested(const QString &message, const speecher::PopupErrorAction &fix);
    // A selection edit to show against the selection, again with each
    // change, a follow-up starting or ending among them.
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

    void setState(DictationState state, const QString &message = {}, const PopupErrorAction &fix = {});
    using Stage = ProviderAttemptIssue::Stage;
    void continueStartupAfterPopup(quint64 generation);
    bool selectProviders();
    void speechChainExhausted(const SpeechFailure &switchFailure);
    void noteProviderIssue(ProviderRole role, const QString &providerId, Stage stage, const ProviderFailure &failure);
    bool offlineFor(ProviderRole role, const QString &providerId) const;
    void noteSignIn(const QString &providerId, bool signedIn);
    void noteRan(ProviderRole role, const QString &providerId);
    ProviderLabels providerLabels() const;
    void continueStartupAfterPreparation();
    void failStartup(const QString &message, const PopupErrorAction &fix);
    void failSession(const QString &message, const PopupErrorAction &fix = {});
    void beginRefinement(quint64 generation);
    void startRefiner();
    void enterRefining();
    void handleRefinementFailure(const ProviderFailure &failure);
    void passOverRefiner(const QString &providerId, Stage stage, const ProviderFailure &failure);
    void retireRefiner();
    void deliverWithoutRefinement();
    void failSelectionEdit(const QString &message);
    void reviewSelectionEdit(const QString &revised);
    void reviseSelectionEdit(const QString &revised);
    void replaceWithRevision();
    void startFollowUp();
    void returnToReview();
    void enterReview();
    void emitReview();
    bool selectionUnchanged();
    void endSpeechAfterFailure(const SpeechFailure &failure);
    // QtAudioInput::stop() spins a nested event loop for the post-roll, during
    // which commands and provider signals are dispatched. Callers settle the
    // session before stopping and recheck it afterwards.
    void stopAudio();
    bool startAudio(QString *error);
    void setMicrophoneOpen(bool open);
    void updateMicrophoneHeld();
    void updateListening();
    // note follows the receipt, such as why an edit went to the clipboard.
    // spokenWords is what Insights records for a reviewed edit, whose
    // instructions are the words spoken; otherwise the text's own.
    void deliverFinal(const QString &text, const QString &note = {}, std::optional<int> spokenWords = {});
    void discard();
    void forgetSession();
    void clearScreenshotContext();
    void resumePausedMedia();
    bool selectTranscriptRefiner(const QString &providerId);
    void disconnectTranscriptRefiner();
    void connectTranscriptRefiner(TranscriptRefiner *refiner);
    void toggleSession(const SessionOverrides &overrides);
    void startSession(const SessionOverrides &overrides);
    SettingsStore *m_settings = nullptr;
    // The live UI settings, which apply the moment a save changes them,
    // mid-session too. Kept here so previews and streamed refinement never
    // read a whole snapshot per word.
    UiSettings m_ui;
    AudioInput *m_audio = nullptr;
    MediaController *m_mediaController = nullptr;
    TargetProvider *m_targetProvider = nullptr;
    ScreenshotContextProvider *m_screenshotProvider = nullptr;
    TextDeliveryAdapter *m_delivery = nullptr;
    ProviderRegistry *m_providers = nullptr;
    TranscriptState *m_transcript = nullptr;
    // The session's speech, through the registry's transcribers.
    DictationSpeech *m_speech = nullptr;
    RefinementChain *m_refinementChain = nullptr;
    QTimer *m_completionTimer = nullptr;
    // The registry owns it and may be destroyed first; never call a dead one.
    QPointer<TranscriptRefiner> m_refiner;
    QVector<QMetaObject::Connection> m_refinerConnections;
    DictationState m_state = DictationState::Idle;
    QString m_lastMessage;
    QString m_speechWarning;
    QString m_lastTranscript;
    EditReview m_review;
    TranscriptPipelineResult m_transcriptPipeline;
    quint64 m_generation = 0;
    quint64 m_audioGeneration = 0;
    quint64 m_continuedStartupGeneration = 0;
    std::optional<AppSettings> m_sessionSettings;
    // What the session chose for itself: a forced Writing Profile.
    RunChoices m_runChoices;
    Target m_target;
    QByteArray m_screenshotData;
    QString m_screenshotMediaType;
    quint64 m_screenshotCaptureGeneration = 0;
    quint64 m_refinementGeneration = 0;
    QString m_refinementStream;
    // The pipeline's bound values by placeholder, for the refinement preview.
    QHash<QString, QString> m_previewReplacements;
    bool m_heardSpeech = false;
    // From Listening to Stopping: the audio a Dictation Session transcribed.
    QElapsedTimer m_listeningClock;
    int m_listeningMs = 0;
    // Each connection to a refiner gets its own revision.
    quint64 m_refinementRevision = 0;
    // The raw transcript is delivered in place of a refinement that was
    // asked for.
    bool m_usedRawTranscript = false;
    ProviderHistory m_providerHistory;
    // What the system last said about the internet.
    Reachability m_reachability = Reachability::Unknown;
    // Inside stopAudio(); a session start waits in m_pendingStart until it
    // returns, unless a stop or cancel drops it first.
    int m_audioStopDepth = 0;
    bool m_microphoneOpen = false;
    bool m_microphoneHeld = false;
    bool m_listening = false;
    std::optional<SessionOverrides> m_pendingStart;
};

} // namespace speecher
