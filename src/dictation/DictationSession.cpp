#include "dictation/DictationSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <memory>
#include <utility>

namespace speecher {
namespace {

QList<BindingRule> withoutNoBindPhrases(const QList<BindingRule> &rules, const QStringList &normalizedNoBindPhrases)
{
    if (normalizedNoBindPhrases.isEmpty()) {
        return rules;
    }

    QSet<QString> excluded;
    for (const QString &phrase : normalizedNoBindPhrases) {
        excluded.insert(phrase);
    }
    QList<BindingRule> filtered;
    for (const BindingRule &rule : rules) {
        if (!excluded.contains(BindingProcessor::normalizedPhrase(rule.phrase))) {
            filtered.append(rule);
        }
    }
    return filtered;
}

RefinementSettings refinementSettingsWithBindingVocabulary(const AppSettings &settings)
{
    RefinementSettings refinement = settings.refinement;
    refinement.bindingVocabulary = BindingProcessor::refinementVocabulary(settings.bindings);
    return refinement;
}

QStringList refinementVocabulary(const AppSettings &settings)
{
    QStringList vocabulary = settings.speech.vocabulary;
    QSet<QString> seen;
    QStringList deduplicated;
    for (const QString &term : vocabulary) {
        const QString cleaned = term.simplified();
        const QString key = cleaned.toCaseFolded();
        if (!cleaned.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            deduplicated.append(cleaned);
        }
    }

    return deduplicated;
}

} // namespace

struct DictationSession::StartupPreparation {
    std::optional<SpeechPrepareJob> speechPrepareJob;
    std::optional<RefinementRefreshJob> refinerRefreshJob;
    SpeechPrepareResult speechResult{true, {}};
    RefinementRefreshResult refinerRefreshResult{true, {}};
    bool refinerRefreshAttempted = false;
    QPointer<QThread> thread;
};

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
{
    connect(m_transcript, &TranscriptState::changed, this, [this](const QString &text) {
        emit previewDisplayChanged(text);
        qInfo() << "transcript changed length=" << text.size();
        emit previewChanged(text);
    });
    connect(m_audio, &AudioInput::levelChanged, this, &DictationSession::audioLevelChanged);
    connect(m_audio, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
        if (m_transcriber && m_state == DictationState::Listening) {
            m_capturedAudio.append(pcm);
            m_transcriber->sendAudio(m_attemptId, pcm);
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
            stopListening();
            return;
        }

        const quint64 generation = m_generation;
        m_audio->stop();
        if (m_transcriber) {
            m_transcriber->cancelAttempt(m_attemptId);
        }
        discardSessionAudio();
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        setState(DictationState::Error, message);
        QTimer::singleShot(1800, this, [this, generation] {
            if (generation != m_generation || m_state != DictationState::Error) {
                return;
            }
            emit popupHideRequested();
            setState(DictationState::Idle);
        });
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
    discardSessionAudio();
    if (m_startupPreparation && m_startupPreparation->thread) {
        m_startupPreparation->thread->requestInterruption();
        m_startupPreparation->thread->quit();
        m_startupPreparation->thread->wait();
    }
}

DictationState DictationSession::state() const
{
    return m_state;
}

QString DictationSession::stateName() const
{
    return dictationStateName(m_state);
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
    toggleSession(std::nullopt);
}

void DictationSession::toggleWithFormat(OutputFormat format)
{
    toggleSession(format);
}

void DictationSession::toggleSession(std::optional<OutputFormat> format)
{
    qInfo().noquote() << "toggle requested state=" + stateName();
    if (m_state == DictationState::Idle || m_state == DictationState::Error) {
        startSession(format);
    } else if (m_state == DictationState::Starting || m_state == DictationState::Listening) {
        stopListening();
    }
}

void DictationSession::startListening()
{
    startSession(std::nullopt);
}

void DictationSession::startListeningWithFormat(OutputFormat format)
{
    startSession(format);
}

void DictationSession::startSession(std::optional<OutputFormat> format)
{
    if (m_state != DictationState::Idle && m_state != DictationState::Error) {
        return;
    }

    AppSettings settings = m_settings->snapshot();
    if (format) {
        settings.output.format = *format;
    }
    QString providerError;
    if (!selectSpeechTranscriber(settings.speech.providerId, &providerError)) {
        setState(DictationState::Error, providerError);
        return;
    }
    if (settings.refinement.providerId != QStringLiteral("none")) {
        selectTranscriptRefiner(settings.refinement.providerId, nullptr);
    }

    ++m_generation;
    ++m_attemptId;
    const quint64 generation = m_generation;
    m_sessionSettings = settings;
    m_retryUsed = false;
    discardSessionAudio();
    clearScreenshotContext();
    m_target = m_targetProvider ? m_targetProvider->capture() : Target{};
    if (settings.refinement.includeScreenshotContext
        && settings.refinement.providerId != QStringLiteral("none")
        && m_screenshotProvider
        && !m_target.secure) {
        m_screenshotCaptureGeneration = generation;
        m_screenshotProvider->capture();
    }
    setState(DictationState::Starting);
    qInfo().noquote() << "startListening speechProvider=" + settings.speech.providerId
                      << "credentialsPath=" + settings.speech.claudeCredentialsPath
                      << "voiceBase=" + settings.speech.claudeEndpointBase;
    m_transcript->clear();
    m_refinedText.clear();
    m_bindingResult = {};
    m_activeBindingRules.clear();
    m_noBindPhrases.clear();
    m_allowPostRefinementBindings = true;
    emit previewDisplayChanged({});
    emit popupFrozenChanged(false);
    emit popupRefiningChanged(false);
    emit popupStatusChanged(QStringLiteral("Listening"));
    emit popupShowRequested();
    if (settings.ui.pauseMediaDuringTranscription) {
        m_mediaController->pausePlaying();
    }

    std::optional<SpeechPrepareJob> speechPrepareJob = m_transcriber->createPrepareJob(settings.speech);
    const bool speechRefreshRequired = speechPrepareJob ? speechPrepareJob->showRefreshIndicator
                                                        : m_transcriber->requiresRefresh(settings.speech);

    std::optional<RefinementRefreshJob> refinerRefreshJob;
    bool refinerRefreshRequired = false;
    if (m_refiner && settings.refinement.providerId != QStringLiteral("none")) {
        refinerRefreshJob = m_refiner->createRefreshJob(settings.refinement);
        refinerRefreshRequired = refinerRefreshJob ? refinerRefreshJob->showRefreshIndicator
                                                   : m_refiner->requiresRefresh(settings.refinement);
    }

    if (speechRefreshRequired || refinerRefreshRequired) {
        emit popupOAuthRefreshRequested();
    }

    SpeechPrepareResult speechPrepared{true, {}};
    if (!speechPrepareJob) {
        speechPrepared = m_transcriber->prepare(settings.speech);
        emit previewDisplayChanged({});
        if (!speechPrepared.ok) {
            failStartup(generation, speechPrepared.message);
            return;
        }
    }

    if (!refinerRefreshJob && refinerRefreshRequired) {
        m_refiner->refresh(settings.refinement);
        emit previewDisplayChanged({});
    }

    if (speechPrepareJob || refinerRefreshJob) {
        startPreparationWorker(generation,
                               settings,
                               std::move(speechPrepareJob),
                               std::move(refinerRefreshJob),
                               speechPrepared);
        return;
    }

    continueStartupAfterPreparation(generation, settings);
}

void DictationSession::stopListening()
{
    if (m_state != DictationState::Starting && m_state != DictationState::Listening) {
        return;
    }
    if (m_state == DictationState::Starting && m_startupPreparation) {
        ++m_generation;
        m_startupPreparation.reset();
        qInfo() << "startup preparation cancelled";
        if (m_transcriber) {
            m_transcriber->cancelAttempt(m_attemptId);
        }
        discardSessionAudio();
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        emit popupHideRequested();
        setState(DictationState::Idle);
        return;
    }
    setState(DictationState::Stopping);
    qInfo() << "stopListening transcriptLength=" << m_transcript->text().size();
    m_audio->stop();
    if (m_transcriber) {
        m_transcriber->finishInput(m_attemptId);
    }
    resumePausedMedia();
}

void DictationSession::setState(DictationState state, const QString &message)
{
    m_state = state;
    m_lastMessage = message;
    const QString label = dictationStateLabel(state, message);
    emit popupStatusChanged(label);
    qInfo().noquote() << "state changed state=" + stateName()
                      << "messagePresent=" + QString::number(!message.isEmpty());
    emit statusChanged(label);
}

void DictationSession::startPreparationWorker(quint64 generation,
                                              const AppSettings &settings,
                                              std::optional<SpeechPrepareJob> speechPrepareJob,
                                              std::optional<RefinementRefreshJob> refinerRefreshJob,
                                              const SpeechPrepareResult &speechPrepared)
{
    auto preparation = std::make_shared<StartupPreparation>();
    preparation->speechPrepareJob = std::move(speechPrepareJob);
    preparation->refinerRefreshJob = std::move(refinerRefreshJob);
    preparation->speechResult = speechPrepared;

    QThread *thread = QThread::create([preparation] {
        if (preparation->speechPrepareJob) {
            preparation->speechResult = preparation->speechPrepareJob->run
                ? preparation->speechPrepareJob->run()
                : SpeechPrepareResult{false, QStringLiteral("Speech provider startup job unavailable")};
        }
        if (preparation->speechResult.ok
            && preparation->refinerRefreshJob) {
            preparation->refinerRefreshAttempted = true;
            preparation->refinerRefreshResult = preparation->refinerRefreshJob->run
                ? preparation->refinerRefreshJob->run()
                : RefinementRefreshResult{false, QStringLiteral("Refinement refresh job unavailable")};
        }
    });
    preparation->thread = thread;
    m_startupPreparation = preparation;

    connect(thread, &QThread::finished, this, [this, generation, settings, preparation] {
        finishStartupPreparation(generation, settings, preparation);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DictationSession::finishStartupPreparation(quint64 generation,
                                                const AppSettings &settings,
                                                const std::shared_ptr<StartupPreparation> &preparation)
{
    if (m_startupPreparation != preparation || generation != m_generation || m_state != DictationState::Starting) {
        qInfo() << "startup preparation result ignored";
        return;
    }
    m_startupPreparation.reset();

    if (preparation->speechPrepareJob && preparation->speechPrepareJob->apply) {
        preparation->speechPrepareJob->apply(preparation->speechResult);
    }
    if (!preparation->speechResult.ok) {
        failStartup(generation, preparation->speechResult.message);
        return;
    }

    if (preparation->refinerRefreshJob && preparation->refinerRefreshJob->apply) {
        preparation->refinerRefreshJob->apply(preparation->refinerRefreshResult);
    }
    if (preparation->refinerRefreshAttempted && !preparation->refinerRefreshResult.ok) {
        qWarning().noquote() << "refinement oauth refresh unavailable status=" + preparation->refinerRefreshResult.message;
    }

    emit previewDisplayChanged({});
    continueStartupAfterPreparation(generation, settings);
}

void DictationSession::continueStartupAfterPreparation(quint64 generation, const AppSettings &settings)
{
    if (generation != m_generation || m_state != DictationState::Starting) {
        qInfo() << "startup continuation skipped stale generation";
        return;
    }

    emit popupListeningIndicatorRequested();

    m_transcriber->startAttempt(m_attemptId, settings.speech);

    QString audioError;
    if (!m_audio->start(&audioError)) {
        qWarning().noquote() << "audio start failed message=" + audioError;
        m_transcriber->cancelAttempt(m_attemptId);
        discardSessionAudio();
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        setState(DictationState::Error, audioError);
        return;
    }
    qInfo() << "audio capture started";
    setState(DictationState::Listening);
}

void DictationSession::failStartup(quint64 generation, const QString &message)
{
    if (generation != m_generation || m_state != DictationState::Starting) {
        return;
    }
    qWarning().noquote() << "speech credentials unavailable message=" + message;
    emit previewDisplayChanged({});
    discardSessionAudio();
    clearScreenshotContext();
    m_sessionSettings.reset();
    resumePausedMedia();
    setState(DictationState::Error, message);
}

void DictationSession::beginRefinement(quint64 generation)
{
    if (generation != m_generation || m_state != DictationState::Stopping) {
        qInfo() << "beginRefinement skipped stale generation";
        return;
    }
    if (m_transcript->isEmpty()) {
        qWarning() << "beginRefinement no transcript captured";
        discardSessionAudio();
        clearScreenshotContext();
        m_sessionSettings.reset();
        setState(DictationState::Error, QStringLiteral("No transcript captured"));
        QTimer::singleShot(1400, this, [this, generation] {
            if (generation != m_generation || m_state != DictationState::Error) {
                return;
            }
            emit popupHideRequested();
            setState(DictationState::Idle);
        });
        return;
    }

    if (!m_sessionSettings) {
        setState(DictationState::Error, QStringLiteral("Dictation session options are unavailable"));
        discardSessionAudio();
        clearScreenshotContext();
        m_sessionSettings.reset();
        return;
    }
    const AppSettings &settings = *m_sessionSettings;
    const bool hasNoBindDirective = BindingProcessor::hasExplicitNoBindDirective(m_transcript->text());
    m_noBindPhrases = BindingProcessor::explicitNoBindPhrases(m_transcript->text(), settings.bindings);
    m_allowPostRefinementBindings = !hasNoBindDirective || !m_noBindPhrases.isEmpty();
    m_activeBindingRules = withoutNoBindPhrases(settings.bindings, m_noBindPhrases);
    m_bindingResult = BindingProcessor::process(m_transcript->text(), m_activeBindingRules);
    if (settings.refinement.providerId == QStringLiteral("none")) {
        qInfo() << "refinement disabled delivering bound length=" << m_bindingResult.boundText.size()
                << "bindingCount=" << m_bindingResult.placeholders.size();
        deliverFinal(m_bindingResult.boundText);
        return;
    }

    if (m_bindingResult.canSkipRefinement) {
        qInfo() << "bindings covered transcript; skipping refinement bindingCount=" << m_bindingResult.placeholders.size();
        deliverFinal(m_bindingResult.boundText);
        return;
    }

    QString providerError;
    if (!selectTranscriptRefiner(settings.refinement.providerId, &providerError)) {
        qWarning().noquote() << "refinement provider unavailable message=" + providerError;
        m_lastMessage = providerError;
        deliverFinal(m_bindingResult.boundText);
        return;
    }

    const RefinementPrepareResult prepared = m_refiner->prepare(settings.refinement);
    if (!prepared.ok) {
        qWarning().noquote() << "refinement auth unavailable status=" + prepared.message;
        m_lastMessage = prepared.message;
        deliverFinal(m_bindingResult.boundText);
        return;
    }

    setState(DictationState::Refining);
    emit popupRefiningChanged(true);
    m_refinedText.clear();
    const RefinementSettings refinement = refinementSettingsWithBindingVocabulary(settings);
    const QStringList vocabulary = refinementVocabulary(settings);
    RefinementContext context;
    context.target = m_target;
    context.writingProfile = inferWritingProfile(
        m_target,
        writingProfileFromName(refinement.defaultWritingProfile));
    context.includeNearbyText = refinement.useTargetContext && !m_target.secure;
    if (refinement.includeScreenshotContext
        && !m_target.secure
        && m_refiner->supportsScreenshotContext(refinement)
        && !m_screenshotData.isEmpty()
        && !m_screenshotMediaType.isEmpty()) {
        context.screenshotData = m_screenshotData;
        context.screenshotMediaType = m_screenshotMediaType;
    }
    if (!refinement.useTargetContext) {
        context.target.nearbyTextBefore.clear();
        context.target.nearbyTextAfter.clear();
    }
    qInfo() << "refinement started provider=" << settings.refinement.providerId
            << "rawLength=" << m_transcript->text().size()
            << "placeholderLength=" << m_bindingResult.placeholderText.size()
            << "bindingCount=" << m_bindingResult.placeholders.size()
            << "noBindCount=" << m_noBindPhrases.size()
            << "vocabularyCount=" << vocabulary.size();
    m_refiner->refine(m_bindingResult.placeholderText,
                      vocabulary,
                      context,
                      refinement);
}

void DictationSession::deliverFinal(const QString &text)
{
    if (!m_sessionSettings) {
        setState(DictationState::Error, QStringLiteral("Dictation session options are unavailable"));
        discardSessionAudio();
        clearScreenshotContext();
        m_sessionSettings.reset();
        return;
    }
    const AppSettings settings = *m_sessionSettings;
    emit popupRefiningChanged(false);
    setState(DictationState::Delivering);
    qInfo() << "deliverFinal length=" << text.size();
    const DeliveryResult result = m_delivery->deliver(
        settings.output,
        makeDeliveryContent(text, settings.output.format),
        m_target);
    discardSessionAudio();
    clearScreenshotContext();
    m_sessionSettings.reset();
    m_target = {};
    if (result.ok) {
        const quint64 generation = m_generation;
        emit popupMessageRequested(result.message);
        emit statusChanged(result.message);
        QTimer::singleShot(1300, this, [this, generation] {
            if (generation != m_generation || m_state != DictationState::Delivering) {
                return;
            }
            emit popupHideRequested();
            setState(DictationState::Idle);
        });
    } else {
        emit popupFrozenChanged(false);
        qWarning().noquote() << "text delivery failed message=" + result.message;
        setState(DictationState::Error, result.message);
    }
}

void DictationSession::resumePausedMedia()
{
    m_mediaController->resumePaused();
}

void DictationSession::discardSessionAudio()
{
    m_capturedAudio.clear();
    m_capturedAudio.squeeze();
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

void DictationSession::retrySpeechAttempt()
{
    if (!m_transcriber || !m_sessionSettings || m_capturedAudio.isEmpty()) {
        return;
    }

    const quint64 retiredAttempt = m_attemptId;
    m_retryUsed = true;
    m_transcriber->cancelAttempt(retiredAttempt);
    ++m_attemptId;
    m_transcript->clear();
    emit popupStatusChanged(QStringLiteral("Retrying transcription"));
    qInfo() << "retrying speech attempt audioChunks=" << m_capturedAudio.size();

    m_transcriber->startAttempt(m_attemptId, m_sessionSettings->speech);
    for (const QByteArray &pcm : std::as_const(m_capturedAudio)) {
        m_transcriber->sendAudio(m_attemptId, pcm);
    }
    if (m_state == DictationState::Stopping) {
        m_transcriber->finishInput(m_attemptId);
    }
}

void DictationSession::handleSpeechFailure(const SpeechFailure &failure)
{
    if (failure.attemptId != m_attemptId) {
        qInfo() << "ignored failure from retired speech attempt" << failure.attemptId;
        return;
    }
    if (m_state != DictationState::Starting
        && m_state != DictationState::Listening
        && m_state != DictationState::Stopping) {
        return;
    }
    if (failure.retryable && !m_retryUsed && !m_capturedAudio.isEmpty()) {
        retrySpeechAttempt();
        return;
    }

    qWarning().noquote() << "speech transcriber failed transcriptEmpty=" << m_transcript->isEmpty()
                         << "message=" + failure.message;
    if (m_state == DictationState::Stopping && !m_transcript->isEmpty()) {
        m_lastMessage = failure.message;
        beginRefinement(m_generation);
        return;
    }

    const quint64 generation = m_generation;
    m_audio->stop();
    m_transcriber->cancelAttempt(m_attemptId);
    discardSessionAudio();
    clearScreenshotContext();
    m_sessionSettings.reset();
    resumePausedMedia();
    setState(DictationState::Error, failure.message);
    QTimer::singleShot(1800, this, [this, generation] {
        if (generation != m_generation || m_state != DictationState::Error) {
            return;
        }
        emit popupHideRequested();
        setState(DictationState::Idle);
    });
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
    if (provider != m_transcriber) {
        connectSpeechTranscriber(provider);
    }
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

void DictationSession::connectSpeechTranscriber(SpeechTranscriber *transcriber)
{
    for (const QMetaObject::Connection &connection : m_transcriberConnections) {
        QObject::disconnect(connection);
    }
    m_transcriberConnections.clear();
    m_transcriber = transcriber;
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::partialTranscript, this, [this](quint64 attemptId, const QString &text) {
        if (attemptId == m_attemptId) {
            m_transcript->setPartial(text);
        }
    });
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::finalTranscript, this, [this](quint64 attemptId, const QString &text) {
        if (attemptId == m_attemptId) {
            m_transcript->commitFinal(text);
        }
    });
    m_transcriberConnections << connect(m_transcriber, &SpeechTranscriber::attemptCompleted, this, [this](quint64 attemptId) {
        if (attemptId == m_attemptId && m_state == DictationState::Stopping) {
            emit popupFrozenChanged(true);
            beginRefinement(m_generation);
        }
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
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::delta, this, [this](const QString &delta) {
        m_refinedText += delta;
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::completed, this, [this](const QString &text) {
        const QString refined = text.trimmed();
        if (refined.isEmpty()) {
            deliverFinal(m_bindingResult.boundText);
            return;
        }

        const QString postBound = m_allowPostRefinementBindings
            ? BindingProcessor::applyBindingsOutsidePlaceholders(refined, m_activeBindingRules)
            : refined;
        const BindingRestoreResult restored = BindingProcessor::restorePlaceholders(postBound, m_bindingResult.placeholders);
        deliverFinal(restored.ok ? restored.text : m_bindingResult.boundText);
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::failed, this, [this](const QString &message) {
        m_lastMessage = message;
        deliverFinal(m_bindingResult.boundText);
    });
}

} // namespace speecher
