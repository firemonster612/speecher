#include "dictation/DictationSession.h"

#include "core/SettingsStore.h"
#include "core/TranscriptState.h"
#include "core/WordPreview.h"
#include "providers/ProviderRegistry.h"

#include <QDebug>
#include <QTimer>

#include <utility>

namespace speecher {
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
        emit previewDisplayChanged(WordPreview::lastWords(text, words));
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
        if (m_transcriber && m_sessionSettings && acceptsAudio) {
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

        m_audio->stop();
        m_audioGeneration = 0;
        if (m_transcriber) {
            m_transcriber->cancelAttempt(m_attemptId);
        }
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        setState(DictationState::Error, message);
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
    } else if (m_state == DictationState::Starting
               || m_state == DictationState::Listening
               || m_state == DictationState::Refining) {
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
    m_heardSpeech = false;
    m_target = {};
    setState(DictationState::Starting);
    qInfo().noquote() << "startListening speechProvider=" + settings.speech.providerId
                      << "credentialsPath=" + settings.speech.claudeCredentialsPath
                      << "voiceBase=" + settings.speech.claudeEndpointBase;
    m_transcript->clear();
    m_refinedText.clear();
    m_transcriptPipeline = {};
    emit previewDisplayChanged({});
    emit popupFrozenChanged(false);
    emit popupRefiningChanged(false);
    emit popupStatusChanged(QStringLiteral("Preparing"));
    emit popupShowRequested(generation);
    QTimer::singleShot(50, this, [this, generation] {
        continueStartupAfterPopup(generation);
    });
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

    const AppSettings settings = *m_sessionSettings;
    clearScreenshotContext();
    m_target = m_targetProvider
        ? m_targetProvider->capture(settings.appRecognitionRules)
        : Target{};
    m_target.category = classifyTarget(m_target, settings.appRecognitionRules);
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
        m_startupRunner->start(generation,
                               std::move(speechPrepareJob),
                               std::move(refinerRefreshJob),
                               speechPrepared);
        return;
    }

    continueStartupAfterPreparation(generation, settings);
}

void DictationSession::stopListening()
{
    if (m_state == DictationState::Error) {
        ++m_generation;
        emit popupHideRequested();
        setState(DictationState::Idle);
        return;
    }
    if (m_state == DictationState::Refining) {
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
    if (m_state != DictationState::Starting && m_state != DictationState::Listening) {
        return;
    }
    if (m_state == DictationState::Starting) {
        ++m_generation;
        m_startupRunner->cancel();
        if (m_transcriber) {
            m_transcriber->cancelAttempt(m_attemptId);
        }
        clearScreenshotContext();
        m_sessionSettings.reset();
        resumePausedMedia();
        emit popupHideRequested();
        setState(DictationState::Idle);
        return;
    }
    setState(DictationState::Stopping, m_lastMessage);
    qInfo() << "stopListening transcriptLength=" << m_transcript->text().size();
    m_audio->stop();
    m_audioGeneration = 0;
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
    if (state == DictationState::Error && !message.isEmpty()) {
        emit popupErrorRequested(message);
    }
    qInfo().noquote() << "state changed state=" + stateName()
                      << "messagePresent=" + QString::number(!message.isEmpty());
    emit statusChanged(label);
}

void DictationSession::finishStartupPreparation(const StartupPreparationResult &result)
{
    if (result.generation != m_generation
        || m_state != DictationState::Starting
        || !m_sessionSettings) {
        qInfo() << "startup preparation result ignored";
        return;
    }

    if (!result.speech.ok) {
        failStartup(result.generation, result.speech.message);
        return;
    }

    if (result.refinerRefreshAttempted && !result.refinerRefresh.ok) {
        qWarning().noquote() << "refinement oauth refresh unavailable status=" + result.refinerRefresh.message;
    }

    emit previewDisplayChanged({});
    continueStartupAfterPreparation(result.generation, *m_sessionSettings);
}

void DictationSession::continueStartupAfterPreparation(quint64 generation, const AppSettings &settings)
{
    if (generation != m_generation || m_state != DictationState::Starting) {
        qInfo() << "startup continuation skipped stale generation";
        return;
    }

    m_transcriber->startAttempt(m_attemptId, settings.speech);

    QString audioError;
    m_audioGeneration = generation;
    if (!m_audio->start(&audioError)) {
        if (m_audioGeneration == generation) {
            m_audioGeneration = 0;
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
            m_audio->stop();
            m_audioGeneration = 0;
        }
        qInfo() << "audio start completed for a cancelled generation";
        return;
    }
    qInfo() << "audio capture started";
    setState(DictationState::Listening);
    emit popupListeningIndicatorRequested();
}

void DictationSession::failStartup(quint64 generation, const QString &message)
{
    if (generation != m_generation || m_state != DictationState::Starting) {
        return;
    }
    qWarning().noquote() << "speech credentials unavailable message=" + message;
    emit previewDisplayChanged({});
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

    const RefinementPrepareResult prepared = m_refiner->prepare(refinement);
    if (!prepared.ok) {
        qWarning().noquote() << "refinement auth unavailable status=" + prepared.message;
        if (pipeline.editsSelection) {
            failSelectionEdit(prepared.message);
            return;
        }
        m_lastMessage = prepared.message;
        deliverFinal(pipeline.deliveryFallback);
        return;
    }

    setState(DictationState::Refining, m_lastMessage);
    m_refinementGeneration = generation;
    emit popupRefiningChanged(true);
    m_refinedText.clear();
    TranscriptPipeline::includeScreenshotContext(pipeline,
                                                 m_refiner->supportsScreenshotContext(refinement),
                                                 m_screenshotData,
                                                 m_screenshotMediaType);
    qInfo() << "refinement started provider=" << settings.refinement.providerId
            << "rawLength=" << m_transcript->text().size()
            << "placeholderLength=" << pipeline.refinementInput.size()
            << "selectionEdit=" << pipeline.editsSelection
            << "selectedLength=" << pipeline.refinementContext.target.selectedText.size()
            << "writingProfile=" << writingProfileName(pipeline.refinementContext.writingProfile)
            << "screenshotIncluded=" << pipeline.refinementContext.hasScreenshot()
            << "bindingCount=" << pipeline.bindingResult.placeholders.size()
            << "noBindCount=" << pipeline.noBindPhrases.size()
            << "vocabularyCount=" << pipeline.refinementVocabulary.size();
    m_refiner->refine(pipeline.refinementInput,
                      pipeline.refinementVocabulary,
                      pipeline.refinementContext,
                      refinement);
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
    m_refinementGeneration = 0;
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
        const QString outcome = usedFallback
            ? QStringLiteral("Used raw transcript • %1").arg(result.message)
            : result.message;
        emit popupMessageRequested(outcome);
        emit statusChanged(outcome);
        m_completionTimer->start(settings.output.completionStatusDurationMs);
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
    if (m_state != DictationState::Starting
        && m_state != DictationState::Listening
        && m_state != DictationState::Stopping) {
        return;
    }
    qWarning().noquote() << "speech transcriber failed transcriptEmpty=" << m_transcript->isEmpty()
                         << "message=" + failure.message;
    if (!m_transcript->isEmpty()
        && (m_state == DictationState::Listening || m_state == DictationState::Stopping)) {
        if (m_state == DictationState::Listening) {
            m_audio->stop();
            m_audioGeneration = 0;
            m_transcriber->cancelAttempt(m_attemptId);
            resumePausedMedia();
            setState(DictationState::Stopping, failure.message);
            emit popupFrozenChanged(true);
            beginRefinement(m_generation);
            return;
        }
        m_lastMessage = failure.message;
        beginRefinement(m_generation);
        return;
    }

    m_audio->stop();
    m_audioGeneration = 0;
    m_transcriber->cancelAttempt(m_attemptId);
    clearScreenshotContext();
    m_sessionSettings.reset();
    resumePausedMedia();
    setState(DictationState::Error, failure.message);
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
        if (attemptId == m_attemptId
            && (m_state == DictationState::Starting
                || m_state == DictationState::Listening
                || m_state == DictationState::Stopping)) {
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
        if (m_state != DictationState::Refining || m_refinementGeneration != m_generation) {
            return;
        }
        m_refinedText += delta;
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
            deliverFinal(m_transcriptPipeline.deliveryFallback);
        }
    });
    m_refinerConnections << connect(m_refiner, &TranscriptRefiner::failed, this, [this](const QString &message) {
        if (m_state != DictationState::Refining || m_refinementGeneration != m_generation) {
            return;
        }
        if (m_transcriptPipeline.editsSelection) {
            failSelectionEdit(message);
            return;
        }
        m_lastMessage = message;
        deliverFinal(m_transcriptPipeline.deliveryFallback);
    });
}

} // namespace speecher
