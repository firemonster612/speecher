#include "dictation/DictationSession.h"

#include "core/ProviderTable.h"
#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WordPreview.h"
#include "core/ProviderReadiness.h"
#include "core/settings/SpokenLanguages.h"
#include "providers/ProviderRegistry.h"
#include "providers/RefinementChain.h"

#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>

#include <numeric>
#include <utility>

namespace speecher {
namespace {
// The audio held for a speech provider still preparing: the connect budget's
// worth, 10 seconds of 16 kHz mono 16-bit PCM.
constexpr qsizetype kMaxPendingAudioBytes = 10 * 16000 * 2;
QString partMissingWarning(const SpeechFailure &failure)
{
    return failure.phase == SpeechPhase::Finalize
        ? QStringLiteral("Part of the dictation may be missing. ") + failure.message
        : QStringLiteral("Part of the dictation may be missing. The connection dropped.");
}
}

DictationSession::DictationSession(SettingsStore *settings,
                                   AudioInput *audio,
                                   MediaController *mediaController,
                                   TextDeliveryAdapter *delivery,
                                   ProviderRegistry *providers,
                                   QObject *parent)
    : DictationSession(settings, audio, mediaController, nullptr, delivery, providers, parent)
{
}

DictationSession::DictationSession(SettingsStore *settings,
                                   AudioInput *audio,
                                   MediaController *mediaController,
                                   TargetProvider *targetProvider,
                                   TextDeliveryAdapter *delivery,
                                   ProviderRegistry *providers,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_audio(audio)
    , m_mediaController(mediaController)
    , m_targetProvider(targetProvider)
    , m_delivery(delivery)
    , m_providers(providers)
    , m_transcript(new TranscriptState(this))
    , m_speech(new SpeechAttempts(providers, SpeechAttempts::Transcribers::Shared, m_transcript, this))
    , m_refinementChain(new RefinementChain(
          [this](const QString &providerId) {
              return selectTranscriptRefiner(providerId) ? m_refiner.data() : nullptr;
          },
          this))
    , m_completionTimer(new QTimer(this))
{
    if (m_settings) {
        m_ui = m_settings->dictationSnapshot().ui;
        connect(m_settings, &SettingsStore::snapshotApplied, this,
                [this] { m_ui = m_settings->dictationSnapshot().ui; });
    }
    m_completionTimer->setSingleShot(true);
    m_completionTimer->setTimerType(Qt::PreciseTimer);
    connect(m_completionTimer, &QTimer::timeout, this, [this] {
        if (m_state != DictationState::Delivering) {
            return;
        }
        emit popupHideRequested();
        setState(DictationState::Idle);
    });
    connect(m_refinementChain, &RefinementChain::passedOver, this, &DictationSession::passOverRefiner);
    connect(m_refinementChain, &RefinementChain::preparing, this, &DictationSession::enterRefining);
    connect(m_refinementChain, &RefinementChain::secretsResolved, this,
            [this](const AppSettings &settings) { m_sessionSettings = settings; });
    connect(m_refinementChain, &RefinementChain::ready, this, &DictationSession::startRefiner);
    connect(m_refinementChain, &RefinementChain::exhausted, this, &DictationSession::deliverWithoutRefinement);
    // A late word never repaints the preview once refinement has it.
    m_speech->setAcceptsText([this] {
        return m_state == DictationState::Starting || m_state == DictationState::Listening
            || m_state == DictationState::Paused || m_state == DictationState::Stopping;
    });
    connect(m_speech, &SpeechAttempts::completed, this, &DictationSession::handleAttemptCompleted);
    connect(m_speech, &SpeechAttempts::failed, this, &DictationSession::handleSpeechFailure);
    SpeechChain *speechChain = m_speech->chain();
    connect(speechChain, &SpeechChain::passedOver, this,
            [this](const QString &providerId, Stage stage, const ProviderFailure &failure) {
                noteProviderIssue(ProviderRole::Speech, providerId, stage, failure);
            });
    connect(speechChain, &SpeechChain::renewingSignIn, this, &DictationSession::popupOAuthRefreshRequested);
    connect(speechChain, &SpeechChain::secretsResolved, this, [this](const AppSettings &settings) {
        if (m_between.awaitingProvider && m_sessionSettings) {
            m_sessionSettings = settings;
        }
    });
    connect(speechChain, &SpeechChain::ready, this, &DictationSession::speechProviderReady);
    connect(speechChain, &SpeechChain::exhausted, this, &DictationSession::speechChainExhausted);
    connect(m_transcript, &TranscriptState::changed, this, [this](const QString &text) {
        const int words = m_ui.previewWords;
        emit previewDisplayChanged(m_ui.transcriptionPreviewEnabled ? WordPreview::lastWords(text, words) : QString());
        qInfo() << "transcript changed length=" << text.size() << "previewWords=" << words;
        emit previewChanged(text);
    });
    connect(m_audio, &AudioInput::levelChanged, this, [this](float level) {
        emit audioLevelChanged(level);
        if (m_state == DictationState::Listening && level >= 0.02f) {
            m_heardSpeech = true;
        }
    });
    connect(m_audio, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        const bool acceptsAudio = m_state == DictationState::Starting
            || m_state == DictationState::Listening
            || m_state == DictationState::Stopping;
        if (!m_sessionSettings || !acceptsAudio) {
            return;
        }
        if (m_between.holdsAudio()) {
            holdPendingAudio(pcm);
            return;
        }
        // None once no provider is left: the speech is ending.
        if (m_speech->transcriber()) {
            m_speech->sendAudio(pcm);
        }
    });
    connect(m_audio, &AudioInput::failed, this, [this](const QString &message) {
        if (m_state != DictationState::Starting && m_state != DictationState::Listening) {
            return;
        }

        qWarning().noquote() << "audio capture failed transcriptEmpty=" << m_transcript->isEmpty()
                             << "message=" + message;
        if (!m_transcript->isEmpty()) {
            m_lastMessage = message;
            m_usedRawTranscript = true;
            stopListening();
            return;
        }

        m_speech->cancel();
        // The next provider may be preparing.
        m_speech->chain()->cancel();
        failSession(message);
        stopAudio();
    });
}

void DictationSession::setScreenshotContextProvider(ScreenshotContextProvider *provider)
{
    if (m_screenshotProvider == provider) {
        return;
    }
    if (m_screenshotProvider) {
        m_screenshotProvider->cancel();
        disconnect(m_screenshotProvider, nullptr, this, nullptr);
    }
    m_screenshotProvider = provider;
    if (!m_screenshotProvider) {
        clearScreenshotContext();
        return;
    }
    connect(m_screenshotProvider,
            &ScreenshotContextProvider::captured,
            this,
            [this](const QByteArray &data, const QString &mediaType) {
                if (!m_sessionSettings
                    || m_screenshotCaptureGeneration != m_generation
                    || !m_sessionSettings->refinement.includeScreenshotContext) {
                    return;
                }
                m_screenshotData = data;
                m_screenshotMediaType = mediaType;
                qInfo() << "screenshot context captured bytes=" << data.size();
            });
    connect(m_screenshotProvider,
            &ScreenshotContextProvider::failed,
            this,
            [this](const QString &message) {
                if (m_screenshotCaptureGeneration == m_generation) {
                    qInfo().noquote() << "screenshot context omitted reason=" + message;
                }
            });
}

DictationSession::~DictationSession()
{
    m_speech->cancel();
    if (m_refiner) {
        m_refiner->cancel();
    }
    clearScreenshotContext();
}

DictationState DictationSession::state() const
{
    return m_state;
}

DictationState DictationSession::reportedState() const
{
    return m_pendingStart ? DictationState::Starting : m_state;
}

QString DictationSession::stateName() const
{
    return dictationStateName(reportedState());
}

bool DictationSession::startPending() const
{
    return m_pendingStart.has_value();
}

bool DictationSession::reviewUp() const
{
    return m_review.isUp();
}

QString DictationSession::lastTranscript() const
{
    return m_lastTranscript.isEmpty() ? m_transcript->text() : m_lastTranscript;
}

int DictationSession::listeningMs() const
{
    return m_listeningMs + (m_state == DictationState::Listening ? int(m_listeningClock.elapsed()) : 0);
}

QString DictationSession::lastMessage() const
{
    return m_lastMessage;
}

SessionResponse DictationSession::response(bool ok, const QString &message) const
{
    return {ok, stateName(), message.isEmpty() ? m_lastMessage : message};
}

void DictationSession::toggle()
{
    toggleSession({});
}

void DictationSession::toggleWith(const SessionOverrides &overrides)
{
    toggleSession(overrides);
}

void DictationSession::toggleSession(const SessionOverrides &overrides)
{
    qInfo().noquote() << "toggle requested state=" + stateName();
    if (m_pendingStart) {
        stopListening();
    } else if (m_state == DictationState::Idle || m_state == DictationState::Error) {
        startSession(overrides);
    } else if (m_state == DictationState::Starting
               || m_state == DictationState::Listening
               || m_state == DictationState::Paused
               || m_state == DictationState::Refining) {
        stopListening();
    } else if (m_state == DictationState::Reviewing) {
        startSession(overrides);
    }
}

void DictationSession::startListening()
{
    startSession({});
}

void DictationSession::startListeningWith(const SessionOverrides &overrides)
{
    startSession(overrides);
}

void DictationSession::startSession(const SessionOverrides &overrides)
{
    // A start while an edit is reviewed, from a toggle or a push-to-talk
    // press, is a follow-up to it, on the edit's own settings: overrides do
    // not apply. One that lands while the microphone stops waits as any start
    // does, the review shown as it is until it begins.
    if (m_state == DictationState::Reviewing && !m_review.followingUp()) {
        if (m_audioStopDepth > 0) {
            m_pendingStart = SessionOverrides{};
        } else {
            startFollowUp();
        }
        return;
    }
    const bool followUp = m_state == DictationState::Reviewing && m_review.followingUp();
    if (m_state != DictationState::Idle && m_state != DictationState::Error && !followUp) {
        return;
    }
    if (m_audioStopDepth > 0) {
        // A start that lands inside the microphone's stop (a cancel's nested
        // event loop) would open it only for the stop to close it again. It
        // waits for the stop instead, and a stop or cancel meanwhile drops it.
        m_pendingStart = overrides;
        return;
    }

    // A follow-up keeps the settings the edit was made with.
    AppSettings settings = followUp ? m_review.revision().settings : m_settings->dictationSnapshot();
    RunChoices choices = followUp ? m_review.revision().choices : RunChoices{};
    if (overrides.outputFormat) {
        settings.output.format = *overrides.outputFormat;
    }
    if (overrides.writingProfile) {
        choices.writingProfile = *overrides.writingProfile;
    }
    if (overrides.spokenLanguage) {
        settings.speech.language = *overrides.spokenLanguage;
    }

    ++m_generation;
    // The last session's attempt has ended; nothing it still sends counts.
    m_speech->forgetAttempt();
    const quint64 generation = m_generation;
    m_sessionSettings = settings;
    m_runChoices = choices;
    m_heardSpeech = false;
    m_target = {};
    m_between = {.awaitingProvider = true};
    // The chains wait for the target, whose Writing Profile may lead them.
    m_refinementChain->begin(kNoRefinementProvider, {});
    m_usedRawTranscript = false;
    m_providerHistory = {};
    m_listeningMs = 0;
    setState(DictationState::Starting);
    qInfo().noquote() << "startListening credentialsPath=" + settings.accounts.claudeCredentialsPath
                      << "voiceBase=" + settings.speech.claudeEndpointBase;
    m_speechWarning.clear();
    m_lastTranscript.clear();
    m_transcript->clear();
    m_transcriptPipeline = {};
    emit previewDisplayChanged({});
    emit popupShowRequested(generation);
    QTimer::singleShot(s_popupPaintFallbackMs, this, [this, generation] {
        continueStartupAfterPopup(generation);
    });
}

int DictationSession::s_popupPaintFallbackMs = 50;

void DictationSession::setPopupPaintFallbackMs(int ms)
{
    s_popupPaintFallbackMs = ms;
}

void DictationSession::popupPresented(quint64 generation)
{
    continueStartupAfterPopup(generation);
}

void DictationSession::continueStartupAfterPopup(quint64 generation)
{
    if (generation != m_generation
        || generation == m_continuedStartupGeneration
        || m_state != DictationState::Starting
        || !m_sessionSettings) {
        return;
    }
    m_continuedStartupGeneration = generation;

    clearScreenshotContext();
    // Capturing can spin an event loop, where a stop or cancel ends this start
    // and drops the session settings: the rules classification reads after it
    // are a copy.
    const QList<AppRecognitionRule> rules = m_sessionSettings->appRecognitionRules;
    // A follow-up edits the same selection, so the Target is the review's:
    // capturing again would also replace the control the provider tracks.
    Target target = m_review.followingUp() ? m_review.revision().target
        : m_targetProvider ? m_targetProvider->capture()
                           : Target{};
    if (generation != m_generation) {
        return;
    }
    m_target = std::move(target);
    m_target.category = classifyTarget(m_target, rules);
    // Lengths only: the text itself stays out of the log.
    qInfo().noquote() << (m_review.followingUp() ? "target kept for follow-up app=" : "target captured app=")
                             + m_target.applicationId
                      << "role=" + m_target.role
                      << "accessible=" << m_target.accessible << "editable=" << m_target.editable
                      << "selectionLength=" << m_target.selectedText.size()
                      << "selectionEdit=" << m_target.hasSelection();
    // The target settles the Writing Profile, and with it the services the
    // session runs and the terms the speech request may carry.
    if (!selectProviders()) {
        return;
    }
    m_sessionSettings->speech.vocabulary =
        TranscriptPipeline::speechVocabulary(*m_sessionSettings, m_runChoices, m_target);
    const AppSettings settings = *m_sessionSettings;
    const RefinementSettings effectiveRefinement =
        TranscriptPipeline::effectiveRefinementSettings(settings, m_runChoices, m_target);
    // Once, for whichever refiner in the chain reads it.
    const QStringList &refiners = m_refinementChain->providerIds();
    const bool chainReadsScreenshots = std::any_of(refiners.cbegin(), refiners.cend(), [this](const QString &id) {
        return m_providers->refinementProviderReadsScreenshots(id);
    });
    if (settings.refinement.includeScreenshotContext
        && effectiveRefinement.style != QStringLiteral("none")
        && m_screenshotProvider
        && chainReadsScreenshots
        && !m_target.secure
        && !m_review.followingUp()) {
        m_screenshotCaptureGeneration = generation;
        m_screenshotProvider->capture();
    }
    if (settings.ui.pauseMediaDuringTranscription) {
        m_mediaController->pausePlaying();
    }

    prepareSpeechProvider();
}

// The target's Writing Profile's own services lead the chains. A fallback
// that is missing or can't listen for the Spoken Language is passed over
// later; the primary stops the dictation, as it always has.
bool DictationSession::selectProviders()
{
    m_sessionSettings = m_providers->withProfileProviders(
        *m_sessionSettings, TranscriptPipeline::writingProfile(*m_sessionSettings, m_runChoices, m_target));
    const AppSettings &settings = *m_sessionSettings;
    qInfo().noquote() << "startListening speechProvider=" + settings.speech.providerId
                      << "refinementProvider=" + settings.refinement.providerId;
    if (!m_speech->acquire(settings.speech.providerId)) {
        failStartup(QStringLiteral("Unknown speech provider: %1").arg(settings.speech.providerId),
                    speechSetupAction(settings.speech.providerId));
        return false;
    }
    if (const QString problem =
            spokenLanguageProblem(settings.speech, m_providers->speechProviderLabel(settings.speech.providerId));
        !problem.isEmpty()) {
        failStartup(problem, {ErrorFix::SettingsPage, QStringLiteral("speech")});
        return false;
    }
    if (settings.refinement.providerId != kNoRefinementProvider) {
        selectTranscriptRefiner(settings.refinement.providerId);
    }
    m_speech->chain()->begin(settings.speech.providerId, settings.speech.fallbackProviderIds);
    m_refinementChain->begin(settings.refinement.providerId, settings.refinement.fallbackProviderIds);
    return true;
}

// Walks the speech chain on from the provider it is on, never back: the
// first provider that prepares takes the next attempt (speechProviderReady).
void DictationSession::prepareSpeechProvider()
{
    m_speech->chain()->prepare(*m_sessionSettings, refinerToRenew());
}

// While starting, the refiner's sign-in renews alongside the speech
// provider's, so refinement need not wait for it later.
TranscriptRefiner *DictationSession::refinerToRenew() const
{
    const bool refines = m_sessionSettings->refinement.providerId != kNoRefinementProvider;
    return m_state == DictationState::Starting && refines ? m_refiner.data() : nullptr;
}

// The prepared provider takes over where the session is: it opens the
// first attempt while starting, and later the attempt for the audio heard
// while it prepared.
void DictationSession::speechProviderReady()
{
    if (!m_between.awaitingProvider || !m_sessionSettings) {
        qInfo() << "speech preparation result ignored";
        return;
    }
    emit previewDisplayChanged({});
    noteSignIn(m_speech->chain()->currentId(), true);
    m_between.awaitingProvider = false;
    switch (m_state) {
    case DictationState::Starting:
        if (m_microphoneStartGeneration == m_generation) {
            resumeAttempt();
        } else {
            continueStartupAfterPreparation(m_generation);
        }
        break;
    case DictationState::Listening:
        resumeAttempt();
        break;
    case DictationState::Paused:
        // What was heard before the pause goes to it now, as a pause would
        // send it; otherwise resume opens its first attempt.
        if (!m_between.pendingAudio.isEmpty()) {
            resumeAttempt();
            if (!m_between.awaitingProvider) {
                m_between.finishingPausedAttempt = true;
                m_speech->finishInput();
            }
        }
        break;
    case DictationState::Stopping:
        attemptEndedWhileStopping();
        break;
    case DictationState::Idle:
    case DictationState::Refining:
    case DictationState::Delivering:
    case DictationState::Error:
    case DictationState::Reviewing:
        break;
    }
}

// No provider is left to take the dictation: the start fails, or what was
// heard is refined as when the only provider fails.
void DictationSession::speechChainExhausted()
{
    m_between.awaitingProvider = false;
    m_between.pendingAudio.clear();
    if (m_state != DictationState::Starting) {
        endSpeechAfterFailure(m_switchFailure);
        return;
    }
    const ProviderAttemptIssue only = m_providerHistory.issues.value(0);
    const QString message = m_speech->failureText(only.message, m_providerHistory.issues);
    if (!m_speech->triedFallbacks(m_providerHistory.issues)) {
        // Without internet, no setting would help.
        failStartup(message, only.offline ? PopupErrorAction{} : speechSetupAction(only.providerId));
        return;
    }
    failStartup(message, {ErrorFix::SettingsPage, QStringLiteral("speech")});
}

// A provider still preparing gets no more than the connect budget's worth of
// audio: past it, it is passed over and the speech ends with what was heard.
void DictationSession::holdPendingAudio(const QByteArray &pcm)
{
    m_between.pendingAudio.append(pcm);
    const qsizetype held = std::accumulate(m_between.pendingAudio.cbegin(), m_between.pendingAudio.cend(), qsizetype(0),
                                           [](qsizetype bytes, const QByteArray &chunk) { return bytes + chunk.size(); });
    if (!m_between.awaitingProvider || held <= kMaxPendingAudioBytes) {
        return;
    }
    qWarning() << "speech provider still preparing past the connect budget";
    m_speech->chain()->cancel();
    noteProviderIssue(ProviderRole::Speech, m_speech->chain()->currentId(), Stage::Prepare,
                      {ProviderFailureKind::Timeout, QStringLiteral("It took too long to start")});
    speechChainExhausted();
}

// Retires the current attempt's provider and walks on to the next.
void DictationSession::switchSpeechProvider(const SpeechFailure &failure)
{
    m_switchFailure = failure;
    retireSpeechAttempt();
    m_speech->chain()->prepareNext(*m_sessionSettings, refinerToRenew());
}

// The current speech provider is done with this dictation, whether another
// takes over or none is left. One that never connected couldn't be reached;
// any other dropped.
void DictationSession::noteSpeechFailure(const SpeechFailure &failure)
{
    const bool connecting = failure.phase == SpeechPhase::Connect || m_state == DictationState::Starting;
    noteProviderIssue(ProviderRole::Speech, m_speech->chain()->currentId(),
                      connecting ? Stage::Connect : Stage::Interrupted,
                      {failure.kind, failure.message, failure.httpStatus});
}

// Nothing the attempt's provider sends from here on counts, and audio waits
// in m_between for the next provider, never in the failed one's buffers.
void DictationSession::retireSpeechAttempt()
{
    m_between.awaitingProvider = true;
    // The retired attempt will never finalise its last utterance.
    m_speech->commitPartial();
    m_speech->release();
}

void DictationSession::noteProviderIssue(ProviderRole role,
                                         const QString &providerId,
                                         Stage stage,
                                         const ProviderFailure &failure)
{
    qWarning().noquote() << "provider unavailable role=" << int(role) << "provider=" + providerId
                         << "stage=" << int(stage) << "message=" + failure.message;
    // Only a failure to connect, or a pass over, while the system says it is
    // offline, for a provider that needs the internet, lets the outcome say
    // "No internet".
    const bool offline =
        (failure.kind == ProviderFailureKind::Network || failure.kind == ProviderFailureKind::Timeout)
        && offlineFor(role, providerId);
    // Once the microphone is open, a speech provider has been sent audio.
    const bool wordsLost = role == ProviderRole::Speech && stage == Stage::Connect
        && m_state != DictationState::Starting;
    m_providerHistory.issues.append({role, providerId, stage, failure.kind, failure.message, offline, wordsLost});
    // Only a sign-in the service turned down is evidence: an unavailable
    // provider may have a keyring it can't read, not a missing sign-in.
    if (failure.kind == ProviderFailureKind::Authentication) {
        noteSignIn(providerId, false);
    }
}

bool DictationSession::offlineFor(ProviderRole role, const QString &providerId) const
{
    return m_sessionSettings && m_speech->chain()->offlineFor(role, providerId, *m_sessionSettings);
}

void DictationSession::noteSignIn(const QString &providerId, bool signedIn)
{
    if (providerSignsIn(providerId)) {
        emit providerSignInObserved(providerId, signedIn);
    }
}

void DictationSession::noteRan(ProviderRole role, const QString &providerId)
{
    QStringList &ran = role == ProviderRole::Speech ? m_providerHistory.speechRan : m_providerHistory.refinementRan;
    if (!ran.contains(providerId)) {
        ran.append(providerId);
    }
}

ProviderLabels DictationSession::providerLabels() const
{
    return m_providers->labels();
}

void DictationSession::setReviewKeys(const ReviewKeys &keys)
{
    if (m_review.setKeys(keys) && m_review.isUp()) {
        emitReview();
    }
}

void DictationSession::setReachability(Reachability reachability)
{
    m_speech->chain()->setReachability(reachability);
    m_refinementChain->setReachability(reachability);
}

void DictationSession::stopListening()
{
    // Stopping a follow-up before it was heard, or while it is refined,
    // leaves the review as it was, as cancelling it does.
    if (m_review.followingUp() && (m_state == DictationState::Starting || m_state == DictationState::Refining)) {
        cancel();
        return;
    }
    m_pendingStart.reset();
    if (m_state == DictationState::Error) {
        ++m_generation;
        emit popupHideRequested();
        setState(DictationState::Idle);
        return;
    }
    if (m_state == DictationState::Refining) {
        m_refinementChain->cancel();
        if (m_refiner) {
            m_refiner->cancel();
        }
        m_refinementGeneration = 0;
        if (m_transcriptPipeline.editsSelection) {
            forgetSession();
            emit popupHideRequested();
            setState(DictationState::Idle);
        } else {
            m_lastMessage = QStringLiteral("Refinement cancelled");
            m_usedRawTranscript = true;
            deliverFinal(m_transcriptPipeline.deliveryFallback);
        }
        return;
    }
    if (m_state == DictationState::Paused) {
        // The microphone is already off; deliver once the paused attempt's
        // words, and any heard after a resume, are in.
        setState(DictationState::Stopping, m_lastMessage);
        resumePausedMedia();
        if (!m_between.finishingPausedAttempt) {
            refineAfterLastAttempt();
        }
        return;
    }
    if (m_state == DictationState::Starting) {
        discard();
        emit popupHideRequested();
        setState(DictationState::Idle);
        stopAudio();
        return;
    }
    if (m_state != DictationState::Listening) {
        return;
    }
    setState(DictationState::Stopping, m_lastMessage);
    qInfo() << "stopListening transcriptLength=" << m_transcript->text().size();
    const quint64 generation = m_generation;
    // Stopping still takes the post-roll that arrives while the microphone
    // stops. Paused media resumes only after it, so the post-roll does not
    // pick it up from the speakers.
    stopAudio();
    resumePausedMedia();
    if (generation != m_generation || m_state != DictationState::Stopping) {
        return;
    }
    // The last attempt ended during the stop and the post-roll since waits:
    // it gets an attempt of its own before refinement.
    if (std::exchange(m_between.endedDuringStop, false)) {
        refineAfterLastAttempt();
        return;
    }
    // A paused attempt still finishing was already told; its end, or the next
    // attempt's, starts refinement. Between providers, the next one's
    // readiness does.
    if (m_speech->transcriber() && !m_between.finishingPausedAttempt && !m_between.awaitingProvider) {
        m_speech->finishInput();
    }
}

// The quit path. stopListening() finalizes — its Refining branch delivers the
// fallback transcript — so quitting must not go through it. No receipt either:
// the popup goes away with the process.
void DictationSession::cancelForShutdown()
{
    m_pendingStart.reset();
    if (m_state == DictationState::Idle) {
        return;
    }
    m_review.end();
    discard();
    emit popupHideRequested();
    setState(DictationState::Idle);
    stopAudio();
}

void DictationSession::cancel()
{
    const bool startDropped = std::exchange(m_pendingStart, std::nullopt).has_value();
    if (m_state == DictationState::Idle || m_state == DictationState::Delivering) {
        return;
    }
    // A follow-up still waiting for the microphone to stop is all a cancel
    // drops; the review stays.
    if (m_state == DictationState::Reviewing && startDropped) {
        return;
    }
    if (m_state == DictationState::Error) {
        stopListening();
        return;
    }
    if (m_state == DictationState::Reviewing) {
        // Keep original: the selection was never touched, so there is
        // nothing to report.
        qInfo() << "selection edit kept the original";
        m_review.end();
        discard();
        emit popupHideRequested();
        setState(DictationState::Idle);
        return;
    }
    if (m_review.followingUp()) {
        qInfo().noquote() << "follow-up cancelled state=" + stateName();
        discard();
        returnToReview();
        stopAudio();
        return;
    }
    qInfo().noquote() << "cancel requested state=" + stateName();
    discard();
    setState(DictationState::Idle);
    emit popupMessageRequested(cancelledOutcomeText(), PopupOutcome::Cancelled);
    const quint64 generation = m_generation;
    // The live setting: discard() has already dropped the session snapshot.
    QTimer::singleShot(m_settings->dictationSnapshot().output.completionStatusDurationMs, Qt::PreciseTimer, this, [this, generation] {
        if (generation == m_generation && m_state == DictationState::Idle) {
            emit popupHideRequested();
        }
    });
    stopAudio();
}

// Bumps the generation and the attempt so every pending completion (startup
// preparation, speech, refinement, screenshot) is stale, cancels the providers,
// and delivers nothing. The microphone is not stopped here: stopping spins an
// event loop, so callers leave the session idle first and stop it last.
void DictationSession::discard()
{
    ++m_generation;
    m_speech->chain()->cancel();
    m_refinementChain->cancel();
    m_speech->cancel();
    if (m_refiner) {
        m_refiner->cancel();
    }
    m_refinementGeneration = 0;
    m_between = {};
    m_completionTimer->stop();
    forgetSession();
    resumePausedMedia();
}

// Drops what the session ran with: its settings, Target, screenshot and
// transcript pipeline.
void DictationSession::forgetSession()
{
    clearScreenshotContext();
    m_sessionSettings.reset();
    m_target = {};
    m_transcriptPipeline = {};
}

// Media the session paused stays paused: the person is still mid-dictation.
void DictationSession::pause()
{
    if (m_state != DictationState::Listening) {
        return;
    }
    qInfo() << "pause requested transcriptLength=" << m_transcript->text().size();
    const quint64 generation = m_generation;
    // The post-roll arrives while the microphone stops and belongs to this
    // attempt; a stop or cancel can land meanwhile.
    stopAudio();
    if (generation != m_generation || m_state != DictationState::Listening) {
        return;
    }
    // Paused again before the last pause's attempt finished: it is still
    // finishing, and what was heard since waits in m_between. Between
    // providers no attempt is open: the next one's readiness sends the words.
    const bool alreadyFinishing = std::exchange(m_between.finishingPausedAttempt, !m_between.awaitingProvider);
    // Paused first: a provider may report the attempt finished from inside
    // finishInput(), and that must not read as a rollover.
    setState(DictationState::Paused);
    emit audioLevelChanged(0.0f);
    if (!alreadyFinishing && !m_between.awaitingProvider) {
        m_speech->finishInput();
    }
}

void DictationSession::togglePause()
{
    if (m_state == DictationState::Paused) {
        resume();
    } else {
        pause();
    }
}

void DictationSession::resume()
{
    if (m_state != DictationState::Paused || !m_sessionSettings) {
        return;
    }
    qInfo() << "resume requested pausedAttemptFinished=" << !m_between.finishingPausedAttempt;
    const quint64 generation = m_generation;
    // Listening, with the next attempt open, before the microphone starts:
    // its first samples arrive while it starts.
    setState(DictationState::Listening);
    // Between providers the attempt opens once the next one is ready.
    const bool opensAttempt = !m_between.finishingPausedAttempt && !m_between.awaitingProvider;
    const bool openedEmptyAttempt = opensAttempt && m_between.pendingAudio.isEmpty();
    if (opensAttempt) {
        resumeAttempt();
    }
    // What a warm microphone kept rolling during the pause stays out.
    m_audio->clearPreRoll();
    QString audioError;
    m_audioGeneration = generation;
    const bool started = startAudio(&audioError);
    // Starting spins an event loop too; a stop or cancel may have ended the
    // session meanwhile, and stopped the microphone itself.
    if (generation != m_generation || m_state != DictationState::Listening || !m_sessionSettings) {
        if (m_audioGeneration == generation) {
            stopAudio();
        }
        return;
    }
    if (!started) {
        m_audioGeneration = 0;
        qWarning().noquote() << "audio restart after pause failed message=" + audioError;
        if (openedEmptyAttempt) {
            // Nothing reached the attempt this resume opened: let it go and
            // deliver what the pause kept, as a stop while paused would.
            m_speech->cancel();
            setState(DictationState::Stopping, m_lastMessage);
            resumePausedMedia();
            refineAfterLastAttempt();
            return;
        }
        stopListening();
    }
}

void DictationSession::resumeAttempt()
{
    startNextAttempt();
    sendPendingAudio();
}

// A provider that fails on the way makes way for the next, which gets what is
// still waiting; what the failed one took is never sent again.
void DictationSession::sendPendingAudio()
{
    while (!m_between.awaitingProvider && !m_between.pendingAudio.isEmpty()) {
        m_speech->sendAudio(m_between.pendingAudio.takeFirst());
    }
}

// Audio heard after a resume reaches an attempt of its own before anything is
// refined: open it, finish it, and refine once it has finished.
void DictationSession::refineAfterLastAttempt()
{
    // Between providers, the next one's readiness comes back here.
    if (m_between.awaitingProvider) {
        return;
    }
    if (!m_between.pendingAudio.isEmpty()) {
        resumeAttempt();
        if (!m_between.awaitingProvider) {
            m_speech->finishInput();
        }
        return;
    }
    beginRefinement(m_generation);
}

void DictationSession::stopAudio()
{
    m_audioGeneration = 0;
    ++m_audioStopDepth;
    m_audio->stop();
    --m_audioStopDepth;
    setMicrophoneOpen(false);
    if (m_audioStopDepth == 0 && m_pendingStart) {
        // After the caller has settled the session it stopped the microphone for.
        QTimer::singleShot(0, this, [this] {
            if (const auto overrides = std::exchange(m_pendingStart, std::nullopt)) {
                startSession(*overrides);
            }
        });
    }
}

bool DictationSession::startAudio(QString *error)
{
    setMicrophoneOpen(true);
    const bool started = m_audio->start(error);
    if (!started) {
        setMicrophoneOpen(false);
    }
    return started;
}

void DictationSession::setMicrophoneOpen(bool open)
{
    m_microphoneOpen = open;
    updateMicrophoneHeld();
}

void DictationSession::updateMicrophoneHeld()
{
    const bool held =
        m_microphoneOpen || m_state == DictationState::Starting || m_state == DictationState::Listening;
    if (std::exchange(m_microphoneHeld, held) != held) {
        emit microphoneChanged(held);
    }
}

// Listening and Paused come only after the first Listening.
void DictationSession::updateListening()
{
    const bool listening = m_state == DictationState::Listening || m_state == DictationState::Paused;
    if (std::exchange(m_listening, listening) != listening) {
        emit listeningChanged(listening);
    }
}

// In Stopping, the last attempt before refinement ended. While the microphone
// is still stopping its post-roll keeps arriving, so stopListening() takes
// over once the stop returns.
void DictationSession::attemptEndedWhileStopping()
{
    if (m_audioStopDepth > 0) {
        m_between.endedDuringStop = true;
        return;
    }
    refineAfterLastAttempt();
}

void DictationSession::setState(DictationState state, const QString &message, const PopupErrorAction &fix)
{
    // A follow-up that fails, before or after it was heard, leaves the
    // review as it was rather than ending in an error.
    if (state == DictationState::Error && m_review.followingUp()) {
        qWarning().noquote() << "follow-up failed message=" + message;
        returnToReview();
        return;
    }
    if (state == DictationState::Listening) {
        m_listeningClock.start();
    } else if (m_state == DictationState::Listening) {
        m_listeningMs += int(m_listeningClock.elapsed());
    }
    m_state = state;
    m_lastMessage = message;
    const QString label = dictationStateLabel(state, message);
    if (state == DictationState::Error && !message.isEmpty()) {
        emit popupErrorRequested(message, fix);
    }
    qInfo().noquote() << "state changed state=" + stateName()
                      << "messagePresent=" + QString::number(!message.isEmpty());
    emit stateChanged(reportedState());
    emit statusChanged(label);
    updateMicrophoneHeld();
    updateListening();
}

void DictationSession::continueStartupAfterPreparation(quint64 generation)
{
    if (generation != m_generation || m_state != DictationState::Starting) {
        qInfo() << "startup continuation skipped stale generation";
        return;
    }

    const QString providerId = m_speech->chain()->currentId();
    noteRan(ProviderRole::Speech, providerId);
    // A provider can fail inside startAttempt(), or as the audio that waited
    // goes to it: the session has moved on to the next one, or ended, and
    // must not open the microphone for this one.
    if (!m_speech->startAttempt(chainSpeechSettings(m_sessionSettings->speech, providerId), {})) {
        return;
    }
    const quint64 attemptId = m_speech->currentId();
    sendPendingAudio();
    if (generation != m_generation || m_state != DictationState::Starting || attemptId != m_speech->currentId()) {
        return;
    }

    QString audioError;
    m_audioGeneration = generation;
    m_microphoneStartGeneration = generation;
    const bool started = startAudio(&audioError);
    m_microphoneStartGeneration = 0;
    if (!started) {
        if (m_audioGeneration == generation) {
            m_audioGeneration = 0;
        }
        if (generation != m_generation
            || m_state != DictationState::Starting
            || !m_sessionSettings) {
            qInfo() << "audio start failed for a cancelled generation";
            return;
        }
        qWarning().noquote() << "audio start failed message=" + audioError;
        m_speech->cancel();
        // A provider that failed while the microphone started may have handed
        // over to one still preparing.
        m_speech->chain()->cancel();
        failSession(audioError);
        return;
    }
    if (generation != m_generation
        || m_state != DictationState::Starting
        || !m_sessionSettings) {
        if (m_audioGeneration == generation) {
            stopAudio();
        }
        qInfo() << "audio start completed for a cancelled generation";
        return;
    }
    qInfo() << "audio capture started";
    setState(DictationState::Listening);
}

void DictationSession::failStartup(const QString &message, const PopupErrorAction &fix)
{
    qWarning().noquote() << "speech unavailable message=" + message;
    emit previewDisplayChanged({});
    failSession(message, fix);
}

// Ends the session in Error; a follow-up goes back to its review instead
// (setState). Paused media plays again.
void DictationSession::failSession(const QString &message, const PopupErrorAction &fix)
{
    clearScreenshotContext();
    m_sessionSettings.reset();
    resumePausedMedia();
    setState(DictationState::Error, message, fix);
}

void DictationSession::beginRefinement(quint64 generation)
{
    if (generation != m_generation || m_state != DictationState::Stopping) {
        qInfo() << "beginRefinement skipped stale generation";
        return;
    }
    if (m_transcript->isEmpty()) {
        qWarning() << "beginRefinement no transcript captured";
        failSession(m_heardSpeech
                        ? QStringLiteral("No transcript was returned. Try again or check the speech connection.")
                        : QStringLiteral("No speech was detected. Check the selected microphone and input level, then try again."));
        return;
    }

    if (!m_sessionSettings) {
        failSession(QStringLiteral("Dictation session options are unavailable"));
        return;
    }
    const AppSettings &settings = *m_sessionSettings;
    Target target = m_target;
    if (m_review.followingUp()) {
        // A follow-up edits the revision under review, not the selection.
        target.selectedText = m_review.revision().edit;
    }
    m_transcriptPipeline = TranscriptPipeline::prepare(m_transcript->text(), settings, m_runChoices, target,
                                                        m_transcript->heardLanguage());
    TranscriptPipelineResult &pipeline = m_transcriptPipeline;
    const RefinementSettings &refinement = pipeline.refinementSettings;
    if (settings.refinement.providerId == kNoRefinementProvider
        || refinement.style == QStringLiteral("none")) {
        if (pipeline.editsSelection) {
            failSelectionEdit(QStringLiteral("Selection editing requires refinement to be enabled"));
            return;
        }
        qInfo() << "refinement disabled delivering bound length=" << pipeline.deliveryFallback.size()
                << "bindingCount=" << pipeline.bindingResult.placeholders.size();
        deliverFinal(pipeline.deliveryFallback);
        return;
    }

    if (!pipeline.editsSelection && pipeline.bindingResult.canSkipRefinement) {
        qInfo() << "bindings covered transcript; skipping refinement bindingCount=" << pipeline.bindingResult.placeholders.size();
        deliverFinal(pipeline.deliveryFallback);
        return;
    }

    m_refinementChain->prepare(settings, refinement);
}

void DictationSession::startRefiner()
{
    const RefinementSettings refinement = m_refinementChain->currentSettings();
    noteSignIn(refinement.providerId, true);

    enterRefining();
    m_refinementStream.clear();
    TranscriptPipelineResult &pipeline = m_transcriptPipeline;
    TranscriptPipeline::includeScreenshotContext(pipeline,
                                                 m_providers->refinementProviderReadsScreenshots(refinement.providerId),
                                                 m_screenshotData,
                                                 m_screenshotMediaType);
    qInfo() << "refinement started provider=" << refinement.providerId
            << "rawLength=" << m_transcript->text().size()
            << "placeholderLength=" << pipeline.refinementInput.size()
            << "selectionEdit=" << pipeline.editsSelection
            << "selectedLength=" << pipeline.refinementContext.target.selectedText.size()
            << "writingProfile=" << pipeline.refinementContext.writingProfile
            << "screenshotIncluded=" << pipeline.refinementContext.hasScreenshot()
            << "bindingCount=" << pipeline.bindingResult.placeholders.size()
            << "noBindCount=" << pipeline.noBindPhrases.size()
            << "vocabularyCount=" << pipeline.refinementVocabulary.size();
    noteRan(ProviderRole::Refinement, refinement.providerId);
    m_refiner->refine(pipeline.refinementInput,
                      pipeline.refinementVocabulary,
                      pipeline.refinementContext,
                      refinement,
                      m_sessionSettings->accounts);
}

void DictationSession::enterRefining()
{
    if (m_state == DictationState::Refining) {
        return;
    }
    setState(DictationState::Refining, m_lastMessage);
    m_refinementGeneration = m_generation;
}

// A refiner that fails to answer for want of a service makes way for the
// next; one that answers unusably or fails otherwise ends refinement as
// before.
void DictationSession::handleRefinementFailure(const ProviderFailure &failure)
{
    if (permitsProviderFallback(failure.kind)) {
        passOverRefiner(m_refinementChain->currentId(), Stage::Connect, failure);
        m_refinementChain->prepareNext(*m_sessionSettings, m_transcriptPipeline.refinementSettings);
        return;
    }
    qWarning().noquote() << "refinement failed message=" + failure.message;
    m_lastMessage = failure.message;
    deliverWithoutRefinement();
}

void DictationSession::passOverRefiner(const QString &providerId, Stage stage, const ProviderFailure &failure)
{
    qWarning().noquote() << "refinement failed message=" + failure.message;
    m_lastMessage = failure.message;
    noteProviderIssue(ProviderRole::Refinement, providerId, stage, failure);
    retireRefiner();
}

// Nothing it sends from now on counts, and the next refiner starts from an
// empty preview. The revision moves on first: cancelling can make a refiner
// emit at once.
void DictationSession::retireRefiner()
{
    ++m_refinementRevision;
    disconnectTranscriptRefiner();
    if (m_refiner) {
        m_refiner->cancel();
    }
    m_refinementStream.clear();
    emit popupRefinementPreviewChanged({});
}

// No refiner gave a result: a selection edit fails with the last reason, a
// dictation is pasted as spoken.
void DictationSession::deliverWithoutRefinement()
{
    if (m_transcriptPipeline.editsSelection) {
        failSelectionEdit(m_lastMessage);
        return;
    }
    m_usedRawTranscript = true;
    deliverFinal(m_transcriptPipeline.deliveryFallback);
}

void DictationSession::failSelectionEdit(const QString &message)
{
    m_refinementGeneration = 0;
    m_target = {};
    m_transcriptPipeline = {};
    failSession(message);
}

// The edit waits for the person to read it against the selection; Replace
// or Keep original ends the review.
void DictationSession::reviewSelectionEdit(const QString &revised)
{
    const QString spoken = m_transcript->text();
    m_review.revise({revised, {spoken}, m_target, *m_sessionSettings, m_runChoices, m_transcriptPipeline, m_speechWarning,
                     m_providerHistory, countWords(spoken), m_listeningMs});
    m_refinementGeneration = 0;
    enterReview();
}

// The review stays up, dimmed, while the follow-up is dictated as a session
// of its own.
void DictationSession::startFollowUp()
{
    qInfo() << "follow-up requested";
    m_review.startFollowUp();
    emitReview();
    startSession({});
}

void DictationSession::returnToReview()
{
    m_review.endFollowUp();
    m_refinementGeneration = 0;
    const EditReview::Revision &revision = m_review.revision();
    m_target = revision.target;
    m_sessionSettings = revision.settings;
    m_runChoices = revision.choices;
    m_transcriptPipeline = revision.pipeline;
    // Nothing the follow-up met belongs in the receipt of the revision kept.
    m_usedRawTranscript = false;
    m_speechWarning = revision.speechWarning;
    m_providerHistory = revision.providerHistory;
    enterReview();
}

void DictationSession::enterReview()
{
    // The controller takes the review's keys as the state changes; when that
    // changes their names, setReviewKeys has already shown the review.
    const ReviewKeys keys = m_review.keys();
    setState(DictationState::Reviewing);
    if (m_review.keys() == keys) {
        emitReview();
    }
}

void DictationSession::emitReview()
{
    emit popupSelectionEditReviewRequested(m_review.presentation());
}

void DictationSession::replaceSelection()
{
    if (m_state != DictationState::Reviewing || !m_sessionSettings) {
        return;
    }
    // The person may have clicked or typed in the Target while reading. The
    // edit replaces only the selection it was made from; any other goes
    // untouched and the edit waits on the clipboard. Read while the review is
    // still up: reading can spin an event loop, where a cancel, a follow-up or
    // another Replace acts on the review as it stands.
    const quint64 generation = m_generation;
    const bool unchanged = selectionUnchanged();
    if (generation != m_generation || !m_review.isUp()) {
        return;
    }
    const EditReview::Revision review = m_review.revision();
    m_review.end();
    m_listeningMs = review.listeningMs;
    m_providerHistory = review.providerHistory;
    if (!unchanged) {
        m_sessionSettings->output.pasteRules = {{PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly}};
        deliverFinal(review.edit, selectionChangedNote(), review.spokenWords);
        return;
    }
    deliverFinal(review.edit, {}, review.spokenWords);
}

// The control the edit was made in must still be the focused one, holding the
// same selection. Without a way to read the Target, the selection is taken as
// it was.
bool DictationSession::selectionUnchanged()
{
    if (!m_targetProvider) {
        return true;
    }
    // Checked first: capturing again replaces the control the provider tracks
    // with whichever is focused now.
    if (m_targetProvider->focusMatch(m_target) != TargetProvider::FocusMatch::Same) {
        return false;
    }
    const Target now = m_targetProvider->capture();
    return now.selectedText == m_target.selectedText && now.selectionStart == m_target.selectionStart
        && now.selectionEnd == m_target.selectionEnd;
}

void DictationSession::deliverFinal(const QString &text, const QString &note, std::optional<int> spokenWords)
{
    if (!m_sessionSettings) {
        failSession(QStringLiteral("Dictation session options are unavailable"));
        return;
    }
    const AppSettings settings = *m_sessionSettings;
    const quint64 generation = m_generation;
    const QString appName = !m_target.applicationName.isEmpty() ? m_target.applicationName
        : !m_target.processName.isEmpty()                      ? m_target.processName
                                                               : QStringLiteral("Unknown app");
    const QString profile = m_transcriptPipeline.refinementContext.writingProfile;
    const QString profileName = isBuiltInWritingProfile(profile)
        ? QString()
        : writingProfileLabel(profile, settings.refinement.writingProfiles);
    // A selection edit delivers the revised selection; what was dictated is
    // the instruction.
    const int words =
        spokenWords.value_or(countWords(m_transcriptPipeline.editsSelection ? m_transcript->text() : text));
    m_refinementGeneration = 0;
    m_lastTranscript = text;
    setState(DictationState::Delivering);
    qInfo() << "deliverFinal length=" << text.size();
    m_settings->recordVocabularyUsage(text);
    const DeliveryResult result = m_delivery->deliver(
        settings.output,
        makeDeliveryContent(text, settings.output.format),
        m_target);
    if (generation != m_generation
        || m_state != DictationState::Delivering
        || !m_sessionSettings) {
        qInfo() << "delivery result ignored for a cancelled generation";
        return;
    }
    clearScreenshotContext();
    m_sessionSettings.reset();
    m_target = {};
    if (result.ok) {
        emit transcriptDelivered(text);
        // The live setting, not the session snapshot: turning insights off
        // mid-session must stop this one being recorded.
        if (m_settings->dictationSnapshot().insightsEnabled) {
            DictationRecord record{QDateTime::currentDateTime(), m_listeningMs, words, appName, profile, profileName};
            record.speechProviders = m_providerHistory.speechRan;
            record.refinementProviders = m_providerHistory.refinementRan;
            emit dictationRecorded(record);
        }
        // A role without fallbacks ends as it always has, its failures unnamed.
        ProviderHistory history = m_providerHistory;
        history.issues.removeIf([this](const ProviderAttemptIssue &issue) {
            const QStringList &chain = issue.role == ProviderRole::Speech ? m_speech->chain()->providerIds()
                                                                          : m_refinementChain->providerIds();
            return chain.size() < 2;
        });
        DictationOutcome outcome = dictationOutcome(result.message,
                                                          result.receipt == DeliveryReceipt::Copied,
                                                          history,
                                                          m_usedRawTranscript,
                                                          m_speechWarning,
                                                          providerLabels());
        if (!note.isEmpty()) {
            outcome.message += popupMessageSeparator() + note;
        }
        m_lastMessage = outcome.message;
        emit popupMessageRequested(outcome.message, outcome.outcome, outcome.fix);
        emit statusChanged(outcome.message);
        // One that offers a fix stays up long enough to read and use it.
        const int durationMs = outcome.fix.fix == ErrorFix::None
            ? settings.output.completionStatusDurationMs
            : std::max(popupErrorDismissMs(outcome.message), settings.output.completionStatusDurationMs);
        m_completionTimer->start(durationMs);
    } else {
        qWarning().noquote() << "text delivery failed message=" + result.message;
        setState(DictationState::Error,
                 m_speechWarning.isEmpty() ? result.message : result.message + popupMessageSeparator() + m_speechWarning);
    }
}

void DictationSession::resumePausedMedia()
{
    m_mediaController->resumePaused();
}

void DictationSession::clearScreenshotContext()
{
    if (m_screenshotProvider) {
        m_screenshotProvider->cancel();
    }
    m_screenshotData.clear();
    m_screenshotMediaType.clear();
    m_screenshotCaptureGeneration = 0;
}

void DictationSession::handleSpeechFailure(const SpeechFailure &failure)
{
    if (failure.kind == ProviderFailureKind::Authentication) {
        noteSignIn(m_speech->chain()->currentId(), false);
    }
    const bool droppedStream = failure.retryable && failure.phase == SpeechPhase::Streaming;
    // A paused attempt that fails to finish (a stream closed during a long
    // pause) has still ended: resume opens a fresh one.
    if (std::exchange(m_between.finishingPausedAttempt, false)) {
        qInfo().noquote() << "paused speech attempt ended without finishing reason=" + failure.message;
        // Its last words may not have arrived.
        m_speechWarning = partMissingWarning(failure);
        // A provider that turned the attempt away, rather than a stream that
        // closed, makes way for the next before the next attempt: at resume,
        // or now for words heard since one. Offline, so does a closed stream.
        const bool attemptNeeded = m_state != DictationState::Stopping || !m_between.pendingAudio.isEmpty();
        const bool offline = offlineFor(ProviderRole::Speech, m_speech->chain()->currentId());
        if ((!droppedStream || offline) && attemptNeeded && m_speech->chain()->fallbackRemains(failure.kind)) {
            noteSpeechFailure(failure);
            switchSpeechProvider(failure);
            return;
        }
        if (m_state == DictationState::Paused) {
            return;
        }
        // Offline with no provider left, the speech ends below.
        if (m_state == DictationState::Listening && !offline) {
            resumeAttempt();
            return;
        }
        if (m_state == DictationState::Stopping) {
            // Nothing is left for it to hear: its part of the dictation ends here.
            if (!attemptNeeded) {
                noteSpeechFailure(failure);
            }
            attemptEndedWhileStopping();
            return;
        }
    }
    if (m_state != DictationState::Starting
        && m_state != DictationState::Listening
        && m_state != DictationState::Stopping) {
        return;
    }
    // Offline, a reconnect could only fail again.
    if (m_state == DictationState::Listening && m_sessionSettings
        && !offlineFor(ProviderRole::Speech, m_speech->chain()->currentId()) && m_speech->takeReconnect(failure)) {
        // A dropped stream mid-sentence is a transient connection loss, not the
        // end of the Dictation Session.
        m_speechWarning = QStringLiteral("Part of the dictation may be missing. The connection dropped.");
        qInfo().noquote() << "speech stream reconnecting attempt=" << m_speech->currentId() + 1
                          << "reason=" + failure.message;
        startNextAttempt();
        return;
    }
    // Reconnects spent, or a failure they can't mend: the provider is done
    // with this dictation, and the next one takes over. Not once stopped,
    // when the last attempt already has all the audio there is and none may
    // be sent again.
    noteSpeechFailure(failure);
    if (m_state != DictationState::Stopping && m_speech->chain()->fallbackRemains(failure.kind)) {
        switchSpeechProvider(failure);
        return;
    }
    endSpeechAfterFailure(failure);
}

// The failure ends the speech: what was heard is refined, or the session
// fails when nothing was.
void DictationSession::endSpeechAfterFailure(const SpeechFailure &failure)
{
    qWarning().noquote() << "speech transcriber failed transcriptEmpty=" << m_transcript->isEmpty()
                         << "message=" + failure.message;
    const bool hearing = m_state == DictationState::Listening || m_state == DictationState::Paused;
    if (!m_transcript->isEmpty() && (hearing || m_state == DictationState::Stopping)) {
        m_speechWarning = partMissingWarning(failure);
        // Refinement may still replace it.
        m_usedRawTranscript = true;
        if (hearing) {
            m_speech->cancel();
            setState(DictationState::Stopping, failure.message);
            const quint64 generation = m_generation;
            stopAudio();
            resumePausedMedia();
            if (generation == m_generation && m_state == DictationState::Stopping) {
                beginRefinement(m_generation);
            }
            return;
        }
        m_lastMessage = failure.message;
        beginRefinement(m_generation);
        return;
    }

    m_speech->cancel();
    // With fallbacks, every provider tried says why it could not take over.
    const QString message = m_speech->failureText(failure.message, m_providerHistory.issues);
    const PopupErrorAction fix = m_speech->triedFallbacks(m_providerHistory.issues)
        ? PopupErrorAction{ErrorFix::SettingsPage, QStringLiteral("speech")}
        : PopupErrorAction{};
    failSession(message, fix);
    stopAudio();
}

void DictationSession::rollOverSpeechAttempt()
{
    // The provider ended a healthy stream on its own (a clean server close)
    // while the person is still talking. That is routine: no warning, and
    // the healthy stream refills the error budget.
    if (!m_speech->takeRollover()) {
        handleSpeechFailure(m_speech->endedTooSoon());
        return;
    }
    qInfo() << "speech stream ended by the provider; rolling over to attempt" << m_speech->currentId() + 1;
    startNextAttempt();
}

// Opens a fresh attempt on the same transcriber while the microphone keeps
// running; the transcriber buffers audio until the new stream is ready.
void DictationSession::startNextAttempt()
{
    const QString providerId = m_speech->chain()->currentId();
    noteRan(ProviderRole::Speech, providerId);
    m_speech->startAttempt(chainSpeechSettings(m_sessionSettings->speech, providerId), {});
}

bool DictationSession::selectTranscriptRefiner(const QString &providerId)
{
    TranscriptRefiner *provider = m_providers->refinementProvider(providerId);
    if (!provider) {
        return false;
    }
    // Always afresh, under a revision of its own: a refiner retired earlier
    // was disconnected.
    connectTranscriptRefiner(provider);
    return true;
}

void DictationSession::handleAttemptCompleted()
{
    const bool pausedAttempt = std::exchange(m_between.finishingPausedAttempt, false);
    if (m_state == DictationState::Stopping) {
        attemptEndedWhileStopping();
    } else if (m_state == DictationState::Listening) {
        pausedAttempt ? resumeAttempt() : rollOverSpeechAttempt();
    }
    // While still paused the words are in; resume opens the next attempt.
}

void DictationSession::disconnectTranscriptRefiner()
{
    for (const QMetaObject::Connection &connection : m_refinerConnections) {
        QObject::disconnect(connection);
    }
    m_refinerConnections.clear();
}

void DictationSession::connectTranscriptRefiner(TranscriptRefiner *refiner)
{
    disconnectTranscriptRefiner();
    m_refiner = refiner;
    const quint64 revision = ++m_refinementRevision;
    const auto current = [this, revision] {
        return m_state == DictationState::Refining && m_refinementGeneration == m_generation
            && m_refinementRevision == revision;
    };
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::delta, this, [this, current](const QString &text) {
        if (!current()) {
            return;
        }
        // Selection edits stream the complete revised document, not the
        // dictated words; previewing it would flash the whole document
        // through the popup a few words at a time.
        if (m_transcriptPipeline.editsSelection) {
            return;
        }
        m_refinementStream += text;
        if (!m_ui.refinementPreviewEnabled) {
            emit popupRefinementPreviewChanged({});
            return;
        }
        // The stream carries SPEECHER_BINDING_n placeholders the final restore
        // pass maps back to their bound values; the preview must not show that
        // internal syntax. Complete tokens are restored here, and a token still
        // streaming in at the tail (possibly with more digits coming) is hidden
        // until it is finished.
        QString preview = m_refinementStream;
        static const QRegularExpression placeholderToken(
            QStringLiteral("SPEECHER_BINDING_[0-9]+"));
        const qsizetype tail = preview.lastIndexOf(QStringLiteral("SPEECHER"));
        if (tail >= 0) {
            const QRegularExpressionMatch match = placeholderToken.match(
                preview, tail, QRegularExpression::NormalMatch,
                QRegularExpression::AnchorAtOffsetMatchOption);
            if (!match.hasMatch() || match.capturedEnd() == preview.size()) {
                preview.truncate(tail);
            }
        }
        // One left-to-right pass over whole-token matches, the token rule
        // final restoration uses (BindingMatcher::restorePlaceholders):
        // each match is replaced through a lookup and the scan continues
        // after the splice, so inserted replacement text is never rescanned
        // and a replacement that itself contains placeholder-shaped text
        // survives here exactly as it does in the delivered result.
        QHash<QString, QString> replacements;
        for (const BindingPlaceholder &placeholder :
             m_transcriptPipeline.bindingResult.placeholders) {
            replacements.insert(placeholder.placeholder, placeholder.replacement);
        }
        QString restored;
        restored.reserve(preview.size());
        qsizetype cursor = 0;
        QRegularExpressionMatchIterator tokens = placeholderToken.globalMatch(preview);
        while (tokens.hasNext()) {
            const QRegularExpressionMatch token = tokens.next();
            restored += preview.mid(cursor, token.capturedStart() - cursor);
            restored += replacements.value(token.captured(), token.captured());
            cursor = token.capturedEnd();
        }
        restored += preview.mid(cursor);
        preview = std::move(restored);
        emit popupRefinementPreviewChanged(WordPreview::lastWords(preview, m_ui.previewWords));
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::completed, this, [this, current](const QString &text) {
        if (!current()) {
            return;
        }
        const std::optional<QString> refined = TranscriptPipeline::restoreRefinedResult(
            m_transcriptPipeline,
            text);
        if (refined) {
            m_lastMessage.clear();
            m_usedRawTranscript = false;
        }
        if (refined && m_transcriptPipeline.editsSelection && m_sessionSettings
            && m_sessionSettings->ui.selectionEditReviewEnabled) {
            reviewSelectionEdit(*refined);
        } else if (refined) {
            deliverFinal(*refined);
        } else if (m_transcriptPipeline.editsSelection) {
            failSelectionEdit(QStringLiteral("The refinement model returned an unusable selection edit"));
        } else {
            // An unusable answer is not a missing service: no other refiner,
            // and the outcome is the plain receipt, as it always was.
            qWarning() << "refinement result could not be restored, delivering fallback";
            deliverFinal(m_transcriptPipeline.deliveryFallback);
        }
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::failed, this, [this, current](const ProviderFailure &failure) {
        if (!current()) {
            return;
        }
        handleRefinementFailure(failure);
    });
}

} // namespace speecher
