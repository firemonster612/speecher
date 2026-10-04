#include "dictation/DictationSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WordPreview.h"
#include "core/settings/FallbackPresentation.h"
#include "core/settings/SpokenLanguages.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>

#include <utility>

namespace speecher {
namespace {
// Dropped speech streams the session reopens before giving up and delivering.
constexpr int kSpeechReconnectsPerSession = 2;
// How long a speech attempt must stream before its end counts as healthy.
constexpr int kDefaultStableAttemptMs = 10000;
QString partMissingWarning(const SpeechFailure &failure)
{
    return failure.phase == QStringLiteral("finalize")
        ? QStringLiteral("Part of the dictation may be missing. ") + failure.message
        : QStringLiteral("Part of the dictation may be missing. The connection dropped.");
}
}

int DictationSession::s_stableAttemptMs = kDefaultStableAttemptMs;

int DictationSession::stableAttemptMs()
{
    return s_stableAttemptMs;
}

void DictationSession::setStableAttemptMs(int ms)
{
    s_stableAttemptMs = ms;
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
    , m_startupRunner(new StartupPreparationRunner(this))
    , m_completionTimer(new QTimer(this))
{
    m_completionTimer->setSingleShot(true);
    m_completionTimer->setTimerType(Qt::PreciseTimer);
    connect(m_completionTimer, &QTimer::timeout, this, [this] {
        if (m_state != DictationState::Delivering) {
            return;
        }
        emit popupHideRequested();
        setState(DictationState::Idle);
    });
    connect(m_startupRunner,
            &StartupPreparationRunner::completed,
            this,
            &DictationSession::finishStartupPreparation);
    connect(m_transcript, &TranscriptState::changed, this, [this](const QString &text) {
        const int words = m_settings ? m_settings->previewWords() : 7;
        const bool showPreview = !m_settings || m_settings->transcriptionPreviewEnabled();
        emit previewDisplayChanged(showPreview ? WordPreview::lastWords(text, words) : QString());
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
        if (!m_transcriber || !m_sessionSettings || !acceptsAudio) {
            return;
        }
        if (m_finishingPausedAttempt || m_attemptEndedDuringStop || m_awaitingSpeechProvider) {
            m_resumeAudio.append(pcm);
            return;
        }
        m_transcriber->sendAudio(m_attemptId, pcm);
    });
    connect(m_audio, &AudioInput::failed, this, [this](const QString &message) {
        if (m_state != DictationState::Starting && m_state != DictationState::Listening) {
            return;
        }

        qWarning().noquote() << "audio capture failed transcriptEmpty=" << m_transcript->isEmpty()
                             << "message=" + message;
        if (!m_transcript->isEmpty()) {
            m_lastMessage = message;
            stopListening();
            return;
        }

        if (m_transcriber) {
            m_transcriber->cancelAttempt(m_attemptId);
        }
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        setState(DictationState::Error, message);
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
    if (m_transcriber) {
        m_transcriber->cancelAttempt(m_attemptId);
    }
    if (m_refiner) {
        m_refiner->cancel();
    }
    clearScreenshotContext();
}

DictationState DictationSession::state() const
{
    return m_state;
}

QString DictationSession::stateName() const
{
    return m_pendingStart ? dictationStateName(DictationState::Starting) : dictationStateName(m_state);
}

bool DictationSession::startPending() const
{
    return m_pendingStart.has_value();
}

QString DictationSession::lastTranscript() const
{
    return m_lastTranscript.isEmpty() ? m_transcript->text() : m_lastTranscript;
}

QString DictationSession::lastMessage() const
{
    return m_lastMessage;
}

QString DictationSession::lastFailure() const
{
    return m_lastFailure;
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
    if (m_state != DictationState::Idle && m_state != DictationState::Error) {
        return;
    }
    if (m_audioStopDepth > 0) {
        // A start that lands inside the microphone's stop (a cancel's nested
        // event loop) would open it only for the stop to close it again. It
        // waits for the stop instead, and a stop or cancel meanwhile drops it.
        m_pendingStart = overrides;
        return;
    }

    AppSettings settings = m_settings->dictationSnapshot();
    if (overrides.outputFormat) {
        settings.output.format = *overrides.outputFormat;
    }
    if (overrides.writingProfile) {
        settings.refinement.sessionWritingProfile = *overrides.writingProfile;
    }
    if (overrides.spokenLanguage) {
        settings.speech.language = *overrides.spokenLanguage;
    }
    // A fallback that is missing or can't listen for the Spoken Language is
    // passed over; the primary stops the dictation, as it always has.
    QString providerError;
    if (!selectSpeechTranscriber(settings.speech.providerId, &providerError)) {
        setState(DictationState::Error, providerError, speechSetupAction(settings.speech.providerId));
        return;
    }
    if (const QString problem = spokenLanguageProblem(settings.speech, m_providers->speechProviderLabel(settings.speech.providerId));
        !problem.isEmpty()) {
        setState(DictationState::Error, problem, {ErrorFix::SettingsPage, QStringLiteral("dictation")});
        return;
    }
    if (settings.refinement.providerId != QStringLiteral("none")) {
        selectTranscriptRefiner(settings.refinement.providerId, nullptr);
    }

    ++m_generation;
    ++m_attemptId;
    const quint64 generation = m_generation;
    m_sessionSettings = settings;
    m_heardSpeech = false;
    m_speechReconnectsLeft = kSpeechReconnectsPerSession;
    m_target = {};
    m_finishingPausedAttempt = false;
    m_attemptEndedDuringStop = false;
    m_resumeAudio.clear();
    m_speechChain = providerChain(ProviderRole::Speech, settings.speech.providerId, settings.speech.fallbackProviderIds);
    m_speechIndex = 0;
    m_awaitingSpeechProvider = true;
    m_providerHistory = {};
    m_listeningMs = 0;
    setState(DictationState::Starting);
    qInfo().noquote() << "startListening speechProvider=" + settings.speech.providerId
                      << "credentialsPath=" + settings.speech.claudeCredentialsPath
                      << "voiceBase=" + settings.speech.claudeEndpointBase;
    m_speechWarning.clear();
    m_lastTranscript.clear();
    m_transcript->clear();
    m_transcriptPipeline = {};
    // Unfreeze before clearing: a front end whose preview honours the frozen
    // flag (macOS, Windows) would otherwise drop this clear and keep showing the
    // previous dictation's delivered words until the first new partial arrives.
    emit popupFrozenChanged(false);
    emit previewDisplayChanged({});
    emit popupRefiningChanged(false);
    emit popupStatusChanged(QStringLiteral("Preparing"));
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
    m_target = m_targetProvider
        ? m_targetProvider->capture(m_sessionSettings->appRecognitionRules)
        : Target{};
    m_target.category = classifyTarget(m_target, m_sessionSettings->appRecognitionRules);
    // The target settles the Writing Profile, and with it the terms the
    // speech request may carry.
    m_sessionSettings->speech.vocabulary =
        TranscriptPipeline::speechVocabulary(*m_sessionSettings, m_target);
    const AppSettings settings = *m_sessionSettings;
    const RefinementSettings effectiveRefinement =
        TranscriptPipeline::effectiveRefinementSettings(settings, m_target);
    if (settings.refinement.includeScreenshotContext
        && settings.refinement.providerId != QStringLiteral("none")
        && effectiveRefinement.style != QStringLiteral("none")
        && m_screenshotProvider
        && m_refiner
        && m_refiner->id() == settings.refinement.providerId
        && m_refiner->supportsScreenshotContext(settings.refinement)
        && !m_target.secure) {
        m_screenshotCaptureGeneration = generation;
        m_screenshotProvider->capture();
    }
    if (settings.ui.pauseMediaDuringTranscription) {
        m_mediaController->pausePlaying();
    }

    prepareSpeechProvider();
}

// Walks the speech chain on from m_speechIndex, never back: the first
// provider that prepares takes the next attempt (speechProviderReady), and
// each one that can't says why.
void DictationSession::prepareSpeechProvider()
{
    for (; m_speechIndex < m_speechChain.size(); ++m_speechIndex) {
        const QString providerId = m_speechChain.at(m_speechIndex);
        const SpeechSettings speech = speechSettingsFor(providerId);
        if (m_speechIndex > 0) {
            if (const QString problem = spokenLanguageProblem(speech, m_providers->speechProviderLabel(providerId));
                !problem.isEmpty()) {
                noteProviderIssue(ProviderRole::Speech, providerId, Stage::LanguageSkipped,
                                  {ProviderFailureKind::Unavailable, problem});
                continue;
            }
        }
        QString providerError;
        if (!selectSpeechTranscriber(providerId, &providerError)) {
            noteProviderIssue(ProviderRole::Speech, providerId, Stage::Prepare,
                              {ProviderFailureKind::Unavailable, providerError});
            continue;
        }
        if (SettingsStore::hasUnreadProviderSecrets(*m_sessionSettings, ProviderRole::Speech, providerId)) {
            m_startupRunner->resolveSecrets(++m_preparationRevision, ProviderRole::Speech, providerId,
                                            *m_sessionSettings);
            return;
        }

        std::optional<SpeechPrepareJob> speechPrepareJob = m_transcriber->createPrepareJob(speech);
        const bool speechRefreshRequired = speechPrepareJob ? speechPrepareJob->showRefreshIndicator
                                                            : m_transcriber->requiresRefresh(speech);
        // While starting, the refiner's sign-in renews alongside, so refinement
        // need not wait for it later.
        std::optional<RefinementRefreshJob> refinerRefreshJob;
        bool refinerRefreshRequired = false;
        const RefinementSettings &refinement = m_sessionSettings->refinement;
        if (m_state == DictationState::Starting && m_refiner && refinement.providerId != QStringLiteral("none")) {
            refinerRefreshJob = m_refiner->createRefreshJob(refinement);
            refinerRefreshRequired = refinerRefreshJob ? refinerRefreshJob->showRefreshIndicator
                                                       : m_refiner->requiresRefresh(refinement);
        }
        if (speechRefreshRequired || refinerRefreshRequired) {
            emit popupOAuthRefreshRequested();
        }

        SpeechPrepareResult speechPrepared{true, {}};
        if (!speechPrepareJob) {
            speechPrepared = m_transcriber->prepare(speech);
            emit previewDisplayChanged({});
            if (!speechPrepared.ok) {
                noteProviderIssue(ProviderRole::Speech, providerId, Stage::Prepare,
                                  {speechPrepared.kind, speechPrepared.message, speechPrepared.httpStatus});
                continue;
            }
        }
        if (!refinerRefreshJob && refinerRefreshRequired) {
            m_refiner->refresh(refinement);
            emit previewDisplayChanged({});
        }
        if (speechPrepareJob || refinerRefreshJob) {
            m_startupRunner->start(++m_preparationRevision,
                                   std::move(speechPrepareJob),
                                   std::move(refinerRefreshJob),
                                   speechPrepared);
            return;
        }
        speechProviderReady();
        return;
    }
    speechChainExhausted();
}

void DictationSession::finishSpeechPreparation(const StartupPreparationResult &result)
{
    if (!m_awaitingSpeechProvider || !m_sessionSettings) {
        qInfo() << "speech preparation result ignored";
        return;
    }
    const QString providerId = m_speechChain.at(m_speechIndex);
    if (!result.speech.ok) {
        noteProviderIssue(ProviderRole::Speech, providerId, Stage::Prepare,
                          {result.speech.kind, result.speech.message, result.speech.httpStatus});
        ++m_speechIndex;
        prepareSpeechProvider();
        return;
    }
    if (result.resolvedSettings) {
        m_sessionSettings = *result.resolvedSettings;
        prepareSpeechProvider();
        return;
    }
    if (result.refinerRefreshAttempted && !result.refinerRefresh.ok) {
        qWarning().noquote() << "refinement oauth refresh unavailable status=" + result.refinerRefresh.message;
    }
    emit previewDisplayChanged({});
    speechProviderReady();
}

// The prepared provider takes over where the session is: it opens the
// first attempt while starting, and later the attempt for the audio heard
// while it prepared.
void DictationSession::speechProviderReady()
{
    noteSignIn(m_speechChain.at(m_speechIndex), true);
    m_awaitingSpeechProvider = false;
    m_speechReconnectsLeft = kSpeechReconnectsPerSession;
    switch (m_state) {
    case DictationState::Starting:
        continueStartupAfterPreparation(m_generation);
        break;
    case DictationState::Listening:
        resumeAttempt();
        break;
    case DictationState::Paused:
        // What was heard before the pause goes to it now, as a pause would
        // send it; otherwise resume opens its first attempt.
        if (!m_resumeAudio.isEmpty()) {
            resumeAttempt();
            if (!m_awaitingSpeechProvider) {
                m_finishingPausedAttempt = true;
                m_transcriber->finishInput(m_attemptId);
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
        break;
    }
}

// No provider is left to take the dictation: the start fails, or what was
// heard is refined as when the only provider fails.
void DictationSession::speechChainExhausted()
{
    m_awaitingSpeechProvider = false;
    m_resumeAudio.clear();
    if (m_state != DictationState::Starting) {
        endSpeechAfterFailure(m_switchFailure);
        return;
    }
    const ProviderAttemptIssue only = m_providerHistory.issues.value(0);
    if (m_speechChain.size() == 1) {
        failStartup(only.message, speechSetupAction(only.providerId));
        return;
    }
    failStartup(noSpeechServiceText(m_providerHistory.issues, providerLabels()),
                {ErrorFix::SettingsPage, QStringLiteral("dictation")});
}

// The current attempt's provider failed in a way the next provider in the
// chain may make up for: retire it and walk on. False, with nothing changed,
// when the failure isn't one a fallback answers or none is left.
bool DictationSession::switchSpeechProvider(Stage stage, const SpeechFailure &failure)
{
    if (!permitsProviderFallback(failure.kind) || m_speechIndex + 1 >= m_speechChain.size()) {
        return false;
    }
    m_switchFailure = failure;
    retireSpeechAttempt();
    noteProviderIssue(ProviderRole::Speech, m_speechChain.at(m_speechIndex), stage,
                      {failure.kind, failure.message, failure.httpStatus});
    ++m_speechIndex;
    prepareSpeechProvider();
    return true;
}

// Nothing the attempt's provider sends from here on counts, and audio waits
// in m_resumeAudio for the next provider, never in the failed one's buffers.
// The id moves on first: cancelling can make a provider emit at once.
void DictationSession::retireSpeechAttempt()
{
    const quint64 retired = m_attemptId++;
    m_awaitingSpeechProvider = true;
    disconnectSpeechTranscriber();
    // The retired attempt will never finalise its last utterance.
    const QString partial = m_transcript->partial();
    if (!partial.isEmpty()) {
        m_transcript->commitFinal(partial);
    }
    m_transcriber->cancelAttempt(retired);
}

SpeechSettings DictationSession::speechSettingsFor(const QString &providerId) const
{
    SpeechSettings speech = m_sessionSettings->speech;
    speech.providerId = providerId;
    return speech;
}

void DictationSession::noteProviderIssue(ProviderRole role,
                                         const QString &providerId,
                                         Stage stage,
                                         const ProviderFailure &failure)
{
    qWarning().noquote() << "provider unavailable role=" << int(role) << "provider=" + providerId
                         << "stage=" << int(stage) << "message=" + failure.message;
    // Only a failure to connect while the system says it is offline, for a
    // provider that needs the internet, lets the outcome say "No internet".
    AppSettings candidate = *m_sessionSettings;
    (role == ProviderRole::Speech ? candidate.speech.providerId : candidate.refinement.providerId) = providerId;
    LiveFacts facts;
    facts.reachability = m_reachability;
    const bool offline =
        (failure.kind == ProviderFailureKind::Network || failure.kind == ProviderFailureKind::Timeout)
        && fallbackProblem(role, providerId, candidate, facts) == FallbackProblem::Offline;
    m_providerHistory.issues.append({role, providerId, stage, failure.kind, failure.message, offline});
    const bool signInMissing = stage == Stage::Prepare && failure.kind == ProviderFailureKind::Unavailable;
    if (failure.kind == ProviderFailureKind::Authentication || signInMissing) {
        noteSignIn(providerId, false);
    }
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
    return [this](ProviderRole role, const QString &providerId) {
        return role == ProviderRole::Speech ? m_providers->speechProviderLabel(providerId)
                                            : m_providers->refinementProviderLabel(providerId);
    };
}

void DictationSession::setReachability(Reachability reachability)
{
    m_reachability = reachability;
}

void DictationSession::stopListening()
{
    m_pendingStart.reset();
    if (m_state == DictationState::Error) {
        ++m_generation;
        emit popupHideRequested();
        setState(DictationState::Idle);
        return;
    }
    if (m_state == DictationState::Refining) {
        m_startupRunner->cancel();
        if (m_refiner) {
            m_refiner->cancel();
        }
        m_refinementGeneration = 0;
        emit popupRefiningChanged(false);
        if (m_transcriptPipeline.editsSelection) {
            clearScreenshotContext();
            m_sessionSettings.reset();
            m_target = {};
            m_transcriptPipeline = {};
            emit popupHideRequested();
            setState(DictationState::Idle);
        } else {
            m_lastMessage = QStringLiteral("Refinement cancelled");
            deliverFinal(m_transcriptPipeline.deliveryFallback);
        }
        return;
    }
    if (m_state == DictationState::Paused) {
        // The microphone is already off; deliver once the paused attempt's
        // words, and any heard after a resume, are in.
        setState(DictationState::Stopping, m_lastMessage);
        resumePausedMedia();
        if (!m_finishingPausedAttempt) {
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
    if (std::exchange(m_attemptEndedDuringStop, false)) {
        refineAfterLastAttempt();
        return;
    }
    // A paused attempt still finishing was already told; its end, or the next
    // attempt's, starts refinement. Between providers, the next one's
    // readiness does.
    if (m_transcriber && !m_finishingPausedAttempt && !m_awaitingSpeechProvider) {
        m_transcriber->finishInput(m_attemptId);
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
    discard();
    emit popupHideRequested();
    setState(DictationState::Idle);
    stopAudio();
}

void DictationSession::cancel()
{
    m_pendingStart.reset();
    if (m_state == DictationState::Idle || m_state == DictationState::Delivering) {
        return;
    }
    if (m_state == DictationState::Error) {
        stopListening();
        return;
    }
    qInfo().noquote() << "cancel requested state=" + stateName();
    discard();
    setState(DictationState::Idle);
    emit popupMessageRequested(cancelledOutcomeText(), PopupOutcome::Cancelled);
    const quint64 generation = m_generation;
    // The live setting: discard() has already dropped the session snapshot.
    QTimer::singleShot(m_settings->completionStatusDurationMs(), Qt::PreciseTimer, this, [this, generation] {
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
    m_startupRunner->cancel();
    if (m_transcriber) {
        m_transcriber->cancelAttempt(m_attemptId);
    }
    ++m_attemptId;
    if (m_refiner) {
        m_refiner->cancel();
    }
    m_refinementGeneration = 0;
    m_finishingPausedAttempt = false;
    m_attemptEndedDuringStop = false;
    m_resumeAudio.clear();
    m_completionTimer->stop();
    clearScreenshotContext();
    m_sessionSettings.reset();
    m_target = {};
    m_transcriptPipeline = {};
    resumePausedMedia();
    emit popupRefiningChanged(false);
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
    // finishing, and what was heard since waits in m_resumeAudio. Between
    // providers no attempt is open: the next one's readiness sends the words.
    const bool alreadyFinishing = std::exchange(m_finishingPausedAttempt, !m_awaitingSpeechProvider);
    // Paused first: a provider may report the attempt finished from inside
    // finishInput(), and that must not read as a rollover.
    setState(DictationState::Paused);
    emit audioLevelChanged(0.0f);
    if (!alreadyFinishing && !m_awaitingSpeechProvider) {
        m_transcriber->finishInput(m_attemptId);
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
    qInfo() << "resume requested pausedAttemptFinished=" << !m_finishingPausedAttempt;
    const quint64 generation = m_generation;
    // Listening, with the next attempt open, before the microphone starts:
    // its first samples arrive while it starts.
    setState(DictationState::Listening);
    // Between providers the attempt opens once the next one is ready.
    const bool opensAttempt = !m_finishingPausedAttempt && !m_awaitingSpeechProvider;
    const bool openedEmptyAttempt = opensAttempt && m_resumeAudio.isEmpty();
    if (opensAttempt) {
        resumeAttempt();
    }
    // What a warm microphone kept rolling during the pause stays out.
    m_audio->clearPreRoll();
    QString audioError;
    m_audioGeneration = generation;
    const bool started = m_audio->start(&audioError);
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
            m_transcriber->cancelAttempt(m_attemptId);
            ++m_attemptId;
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
    // A provider that fails on the way makes way for the next, which gets
    // what is still waiting; what the failed one took is never sent again.
    while (!m_awaitingSpeechProvider && !m_resumeAudio.isEmpty()) {
        m_transcriber->sendAudio(m_attemptId, m_resumeAudio.takeFirst());
    }
}

// Audio heard after a resume reaches an attempt of its own before anything is
// refined: open it, finish it, and refine once it has finished.
void DictationSession::refineAfterLastAttempt()
{
    // Between providers, the next one's readiness comes back here.
    if (m_awaitingSpeechProvider) {
        return;
    }
    if (!m_resumeAudio.isEmpty()) {
        resumeAttempt();
        if (!m_awaitingSpeechProvider) {
            m_transcriber->finishInput(m_attemptId);
        }
        return;
    }
    emit popupFrozenChanged(true);
    beginRefinement(m_generation);
}

void DictationSession::stopAudio()
{
    m_audioGeneration = 0;
    ++m_audioStopDepth;
    m_audio->stop();
    --m_audioStopDepth;
    if (m_audioStopDepth == 0 && m_pendingStart) {
        // After the caller has settled the session it stopped the microphone for.
        QTimer::singleShot(0, this, [this] {
            if (const auto overrides = std::exchange(m_pendingStart, std::nullopt)) {
                startSession(*overrides);
            }
        });
    }
}

// In Stopping, the last attempt before refinement ended. While the microphone
// is still stopping its post-roll keeps arriving, so stopListening() takes
// over once the stop returns.
void DictationSession::attemptEndedWhileStopping()
{
    if (m_audioStopDepth > 0) {
        m_attemptEndedDuringStop = true;
        return;
    }
    refineAfterLastAttempt();
}

void DictationSession::setState(DictationState state, const QString &message, const PopupErrorAction &fix)
{
    if (state == DictationState::Listening) {
        m_listeningClock.start();
    } else if (m_state == DictationState::Listening) {
        m_listeningMs += int(m_listeningClock.elapsed());
    }
    m_state = state;
    m_lastMessage = message;
    if (state == DictationState::Starting) {
        m_lastFailure.clear();
    } else if (state == DictationState::Error && !message.isEmpty()) {
        m_lastFailure = message.simplified();
    }
    const QString label = dictationStateLabel(state, message);
    emit popupStatusChanged(label);
    if (state == DictationState::Error && !message.isEmpty()) {
        emit popupErrorRequested(message, fix);
    }
    qInfo().noquote() << "state changed state=" + stateName()
                      << "messagePresent=" + QString::number(!message.isEmpty());
    emit stateChanged(stateName());
    emit statusChanged(label);
}

void DictationSession::finishStartupPreparation(const StartupPreparationResult &result)
{
    if (result.revision != m_preparationRevision) {
        qInfo() << "startup preparation result ignored";
        return;
    }
    if (result.role == ProviderRole::Refinement) {
        finishRefinerPreparation(result);
    } else {
        finishSpeechPreparation(result);
    }
}

void DictationSession::continueStartupAfterPreparation(quint64 generation)
{
    if (generation != m_generation || m_state != DictationState::Starting) {
        qInfo() << "startup continuation skipped stale generation";
        return;
    }

    m_attemptBaseText.clear();
    m_attemptClock.start();
    const quint64 attemptId = m_attemptId;
    const QString providerId = m_speechChain.at(m_speechIndex);
    noteRan(ProviderRole::Speech, providerId);
    m_transcriber->startAttempt(attemptId, speechSettingsFor(providerId));
    // A provider can fail inside startAttempt(): the session has moved on to
    // the next one, or ended, and must not open the microphone for this one.
    if (generation != m_generation || m_state != DictationState::Starting || attemptId != m_attemptId) {
        return;
    }

    QString audioError;
    m_audioGeneration = generation;
    if (!m_audio->start(&audioError)) {
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
        m_transcriber->cancelAttempt(m_attemptId);
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        setState(DictationState::Error, audioError);
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
    emit popupListeningIndicatorRequested();
}

void DictationSession::failStartup(const QString &message, const PopupErrorAction &fix)
{
    qWarning().noquote() << "speech unavailable message=" + message;
    emit previewDisplayChanged({});
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
        clearScreenshotContext();
        m_sessionSettings.reset();
        setState(DictationState::Error,
                 m_heardSpeech
                     ? QStringLiteral("No transcript was returned. Try again or check the speech connection.")
                     : QStringLiteral("No speech was detected. Check the selected microphone and input level, then try again."));
        return;
    }

    if (!m_sessionSettings) {
        setState(DictationState::Error, QStringLiteral("Dictation session options are unavailable"));
        clearScreenshotContext();
        m_sessionSettings.reset();
        return;
    }
    const AppSettings &settings = *m_sessionSettings;
    m_transcriptPipeline = TranscriptPipeline::prepare(m_transcript->text(),
                                                       settings,
                                                       m_target);
    TranscriptPipelineResult &pipeline = m_transcriptPipeline;
    const RefinementSettings &refinement = pipeline.refinementSettings;
    if (settings.refinement.providerId == QStringLiteral("none")
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

    QString providerError;
    if (!selectTranscriptRefiner(settings.refinement.providerId, &providerError)) {
        qWarning().noquote() << "refinement provider unavailable message=" + providerError;
        if (pipeline.editsSelection) {
            failSelectionEdit(providerError);
            return;
        }
        m_lastMessage = providerError;
        deliverFinal(pipeline.deliveryFallback);
        return;
    }

    prepareRefiner();
}

// The refiner's keys come from the keyring, and an expired sign-in renews,
// on the worker: never on this thread, where prepare() would do both.
void DictationSession::prepareRefiner()
{
    const QString providerId = m_sessionSettings->refinement.providerId;
    if (SettingsStore::hasUnreadProviderSecrets(*m_sessionSettings, ProviderRole::Refinement, providerId)) {
        enterRefining();
        m_startupRunner->resolveSecrets(++m_preparationRevision, ProviderRole::Refinement, providerId,
                                        *m_sessionSettings);
        return;
    }
    if (std::optional<RefinementRefreshJob> refresh = m_refiner->createRefreshJob(refinerSettings())) {
        enterRefining();
        m_startupRunner->start(++m_preparationRevision, std::nullopt, std::move(refresh), {true, {}},
                               ProviderRole::Refinement);
        return;
    }
    startRefiner();
}

void DictationSession::finishRefinerPreparation(const StartupPreparationResult &result)
{
    if (m_state != DictationState::Refining || m_refinementGeneration != m_generation || !m_sessionSettings) {
        qInfo() << "refinement preparation result ignored";
        return;
    }
    if (result.refinerRefreshAttempted && !result.refinerRefresh.ok) {
        handleRefinementFailure(
            {result.refinerRefresh.kind, result.refinerRefresh.message, result.refinerRefresh.httpStatus});
        return;
    }
    if (result.resolvedSettings) {
        m_sessionSettings = *result.resolvedSettings;
        prepareRefiner();
        return;
    }
    startRefiner();
}

void DictationSession::startRefiner()
{
    const RefinementSettings refinement = refinerSettings();
    const RefinementPrepareResult prepared = m_refiner->prepare(refinement);
    if (!prepared.ok) {
        handleRefinementFailure({prepared.kind, prepared.message, prepared.httpStatus});
        return;
    }

    enterRefining();
    m_refinementStream.clear();
    TranscriptPipelineResult &pipeline = m_transcriptPipeline;
    TranscriptPipeline::includeScreenshotContext(pipeline,
                                                 m_refiner->supportsScreenshotContext(refinement),
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
    m_refiner->refine(pipeline.refinementInput,
                      pipeline.refinementVocabulary,
                      pipeline.refinementContext,
                      refinement);
}

void DictationSession::enterRefining()
{
    if (m_state == DictationState::Refining) {
        return;
    }
    setState(DictationState::Refining, m_lastMessage);
    m_refinementGeneration = m_generation;
    emit popupRefiningChanged(true);
}

// The pipeline's refinement settings with the keys read since it was built.
RefinementSettings DictationSession::refinerSettings() const
{
    RefinementSettings refinement = m_transcriptPipeline.refinementSettings;
    refinement.endpoint.apiKey = m_sessionSettings->refinement.endpoint.apiKey;
    refinement.cliproxyApiKey = m_sessionSettings->refinement.cliproxyApiKey;
    return refinement;
}

void DictationSession::handleRefinementFailure(const ProviderFailure &failure)
{
    qWarning().noquote() << "refinement failed message=" + failure.message;
    if (m_transcriptPipeline.editsSelection) {
        failSelectionEdit(failure.message);
        return;
    }
    m_lastMessage = failure.message;
    deliverFinal(m_transcriptPipeline.deliveryFallback);
}

void DictationSession::failSelectionEdit(const QString &message)
{
    m_refinementGeneration = 0;
    emit popupRefiningChanged(false);
    clearScreenshotContext();
    m_sessionSettings.reset();
    m_target = {};
    m_transcriptPipeline = {};
    setState(DictationState::Error, message);
}

void DictationSession::deliverFinal(const QString &text)
{
    if (!m_sessionSettings) {
        setState(DictationState::Error, QStringLiteral("Dictation session options are unavailable"));
        clearScreenshotContext();
        m_sessionSettings.reset();
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
    const int words = countWords(m_transcriptPipeline.editsSelection ? m_transcript->text() : text);
    m_refinementGeneration = 0;
    m_lastTranscript = text;
    const bool usedFallback = !m_lastMessage.isEmpty();
    emit popupRefiningChanged(false);
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
        if (m_settings->insightsEnabled()) {
            emit dictationRecorded(
                {QDateTime::currentDateTime(), m_listeningMs, words, appName, profile, profileName});
        }
        QString outcome = usedFallback
            ? QStringLiteral("Used raw transcript • %1").arg(result.message)
            : result.message;
        if (!m_speechWarning.isEmpty()) {
            outcome += QStringLiteral(" • ") + m_speechWarning;
        }
        m_lastMessage = outcome;
        emit popupMessageRequested(outcome,
                                   usedFallback ? PopupOutcome::Fallback
                                   : result.receipt == DeliveryReceipt::Copied
                                       ? PopupOutcome::Copied
                                       : PopupOutcome::Inserted);
        emit statusChanged(outcome);
        m_completionTimer->start(settings.output.completionStatusDurationMs);
    } else {
        emit popupFrozenChanged(false);
        qWarning().noquote() << "text delivery failed message=" + result.message;
        setState(DictationState::Error,
                 m_speechWarning.isEmpty() ? result.message : result.message + QStringLiteral(" • ") + m_speechWarning);
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
    if (failure.attemptId != m_attemptId) {
        qInfo() << "ignored failure from retired speech attempt" << failure.attemptId;
        return;
    }
    if (failure.kind == ProviderFailureKind::Authentication) {
        noteSignIn(m_speechChain.at(m_speechIndex), false);
    }
    const bool droppedStream = failure.retryable && failure.phase == QStringLiteral("streaming");
    // A paused attempt that fails to finish (a stream closed during a long
    // pause) has still ended: resume opens a fresh one.
    if (std::exchange(m_finishingPausedAttempt, false)) {
        qInfo().noquote() << "paused speech attempt ended without finishing reason=" + failure.message;
        // Its last words may not have arrived.
        m_speechWarning = partMissingWarning(failure);
        // A provider that turned the attempt away, rather than a stream that
        // closed, makes way for the next before the next attempt: at resume,
        // or now for words heard since one.
        const bool attemptNeeded = m_state != DictationState::Stopping || !m_resumeAudio.isEmpty();
        if (!droppedStream && attemptNeeded && switchSpeechProvider(Stage::Interrupted, failure)) {
            return;
        }
        if (m_state == DictationState::Paused) {
            return;
        }
        if (m_state == DictationState::Listening) {
            resumeAttempt();
            return;
        }
        if (m_state == DictationState::Stopping) {
            attemptEndedWhileStopping();
            return;
        }
    }
    if (m_state != DictationState::Starting
        && m_state != DictationState::Listening
        && m_state != DictationState::Stopping) {
        return;
    }
    const bool reconnectable = m_state == DictationState::Listening && droppedStream && m_sessionSettings;
    if (reconnectable) {
        refillReconnectsIfAttemptWasStable();
    }
    if (reconnectable && m_speechReconnectsLeft > 0) {
        // A dropped stream mid-sentence is a transient connection loss, not the
        // end of the Dictation Session.
        m_speechWarning = QStringLiteral("Part of the dictation may be missing. The connection dropped.");
        --m_speechReconnectsLeft;
        qInfo().noquote() << "speech stream reconnecting attempt=" << m_attemptId + 1
                          << "reason=" + failure.message;
        startNextAttempt();
        return;
    }
    // Reconnects spent, or a failure they can't mend: the next provider
    // takes over. Not once stopped, when the last attempt already has all
    // the audio there is and none may be sent again.
    if (m_state != DictationState::Stopping
        && switchSpeechProvider(m_state == DictationState::Starting ? Stage::Connect : Stage::Interrupted, failure)) {
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
        if (hearing) {
            m_transcriber->cancelAttempt(m_attemptId);
            setState(DictationState::Stopping, failure.message);
            emit popupFrozenChanged(true);
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

    m_transcriber->cancelAttempt(m_attemptId);
    clearScreenshotContext();
    // With fallbacks, every provider tried says why it could not take over.
    const bool chainTried = m_speechChain.size() > 1 && !m_providerHistory.issues.isEmpty();
    const QString message = chainTried ? noSpeechServiceText(m_providerHistory.issues, providerLabels())
                                       : failure.message;
    const PopupErrorAction fix =
        chainTried ? PopupErrorAction{ErrorFix::SettingsPage, QStringLiteral("dictation")} : PopupErrorAction{};
    m_sessionSettings.reset();
    resumePausedMedia();
    setState(DictationState::Error, message, fix);
    stopAudio();
}

void DictationSession::rollOverSpeechAttempt()
{
    if (!attemptWasStable()) {
        // A stream the provider ends within seconds of starting is a refusal,
        // not a rollover; treating it as a drop stops it looping unbounded.
        handleSpeechFailure({m_attemptId,
                             QStringLiteral("The speech stream ended within seconds of starting"),
                             true,
                             QStringLiteral("streaming"),
                             ProviderFailureKind::Network});
        return;
    }
    // The provider ended a healthy stream on its own (a clean server close)
    // while the person is still talking. That is routine:
    // no warning, and the healthy stream refills the error budget.
    refillReconnectsIfAttemptWasStable();
    qInfo() << "speech stream ended by the provider; rolling over to attempt" << m_attemptId + 1;
    startNextAttempt();
}

// Opens a fresh attempt on the same transcriber while the microphone keeps
// running; the transcriber buffers audio until the new stream is ready.
void DictationSession::startNextAttempt()
{
    ++m_attemptId;
    // The ended stream will never finalise the current utterance.
    const QString partial = m_transcript->partial();
    if (!partial.isEmpty()) {
        m_transcript->commitFinal(partial);
    }
    // A whole-attempt transcript from the new attempt covers only the audio
    // from here on; the text committed so far must survive.
    m_attemptBaseText = m_transcript->text();
    m_attemptClock.start();
    const QString providerId = m_speechChain.at(m_speechIndex);
    noteRan(ProviderRole::Speech, providerId);
    m_transcriber->startAttempt(m_attemptId, speechSettingsFor(providerId));
}

// The reconnect budget limits streams that keep failing; one that streamed
// for a while before it ended was healthy.
void DictationSession::refillReconnectsIfAttemptWasStable()
{
    if (attemptWasStable()) {
        m_speechReconnectsLeft = kSpeechReconnectsPerSession;
    }
}

bool DictationSession::attemptWasStable() const
{
    return m_attemptClock.isValid() && m_attemptClock.elapsed() >= s_stableAttemptMs;
}

bool DictationSession::selectSpeechTranscriber(const QString &providerId, QString *error)
{
    SpeechTranscriber *provider = m_providers->speechProvider(providerId);
    if (!provider) {
        if (error) {
            *error = QStringLiteral("Unknown speech provider: %1").arg(providerId);
        }
        return false;
    }
    // Always afresh: a provider retired earlier was disconnected.
    connectSpeechTranscriber(provider);
    return true;
}

bool DictationSession::selectTranscriptRefiner(const QString &providerId, QString *error)
{
    TranscriptRefiner *provider = m_providers->refinementProvider(providerId);
    if (!provider) {
        if (error) {
            *error = QStringLiteral("Unknown refinement provider: %1").arg(providerId);
        }
        return false;
    }
    if (provider != m_refiner) {
        connectTranscriptRefiner(provider);
    }
    return true;
}

void DictationSession::disconnectSpeechTranscriber()
{
    for (const QMetaObject::Connection &connection : m_transcriberConnections) {
        QObject::disconnect(connection);
    }
    m_transcriberConnections.clear();
}

void DictationSession::connectSpeechTranscriber(SpeechTranscriber *transcriber)
{
    disconnectSpeechTranscriber();
    m_transcriber = transcriber;
    // Each check names the states speech still counts in, so a late word
    // never repaints the preview once refinement has it.
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::partialTranscript, this, [this](quint64 attemptId, const QString &text) {
        if (attemptId == m_attemptId
            && (m_state == DictationState::Starting
                || m_state == DictationState::Listening
                || m_state == DictationState::Paused
                || m_state == DictationState::Stopping)) {
            m_transcript->setPartial(text);
        }
    });
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::finalTranscript, this, [this](quint64 attemptId, const QString &text) {
        if (attemptId == m_attemptId
            && (m_state == DictationState::Starting
                || m_state == DictationState::Listening
                || m_state == DictationState::Paused
                || m_state == DictationState::Stopping)) {
            m_transcript->commitFinal(text);
        }
    });
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::attemptTranscript, this, [this](quint64 attemptId, const QString &text) {
        if (attemptId == m_attemptId
            && (m_state == DictationState::Starting
                || m_state == DictationState::Listening
                || m_state == DictationState::Paused
                || m_state == DictationState::Stopping)) {
            m_transcript->replaceFinals(m_attemptBaseText.isEmpty()
                                            ? text
                                            : m_attemptBaseText + QLatin1Char(' ') + text);
        }
    });
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::attemptCompleted, this, [this](quint64 attemptId) {
        if (attemptId != m_attemptId) {
            return;
        }
        const bool pausedAttempt = std::exchange(m_finishingPausedAttempt, false);
        if (m_state == DictationState::Stopping) {
            attemptEndedWhileStopping();
        } else if (m_state == DictationState::Listening) {
            pausedAttempt ? resumeAttempt() : rollOverSpeechAttempt();
        }
        // While still paused the words are in; resume opens the next attempt.
    });
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::failed, this, &DictationSession::handleSpeechFailure);
}

void DictationSession::connectTranscriptRefiner(TranscriptRefiner *refiner)
{
    for (const QMetaObject::Connection &connection : m_refinerConnections) {
        QObject::disconnect(connection);
    }
    m_refinerConnections.clear();
    m_refiner = refiner;
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::delta, this, [this](const QString &text) {
        if (m_state != DictationState::Refining || m_refinementGeneration != m_generation) {
            return;
        }
        // Selection edits stream the complete revised document, not the
        // dictated words; previewing it would flash the whole document
        // through the popup a few words at a time.
        if (m_transcriptPipeline.editsSelection) {
            return;
        }
        m_refinementStream += text;
        if (m_settings && !m_settings->refinementPreviewEnabled()) {
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
        const int words = m_settings ? m_settings->previewWords() : 7;
        emit popupRefinementPreviewChanged(WordPreview::lastWords(preview, words));
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::completed, this, [this](const QString &text) {
        if (m_state != DictationState::Refining || m_refinementGeneration != m_generation) {
            return;
        }
        const std::optional<QString> refined = TranscriptPipeline::restoreRefinedResult(
            m_transcriptPipeline,
            text);
        if (refined) {
            m_lastMessage.clear();
            deliverFinal(*refined);
        } else if (m_transcriptPipeline.editsSelection) {
            failSelectionEdit(QStringLiteral("The refinement model returned an unusable selection edit"));
        } else {
            qWarning() << "refinement result could not be restored, delivering fallback";
            deliverFinal(m_transcriptPipeline.deliveryFallback);
        }
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::failed, this, [this](const ProviderFailure &failure) {
        if (m_state != DictationState::Refining || m_refinementGeneration != m_generation) {
            return;
        }
        handleRefinementFailure(failure);
    });
}

} // namespace speecher
