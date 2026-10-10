#include "recording/RecordingSession.h"

#include "core/EchoCanceller.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "platform/audio/AudioPcmConverter.h"
#include "providers/ProviderRegistry.h"
#include "recording/RecordingPresentation.h"
#include "transcribe/FileTranscriptionSession.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace speecher {
namespace {

// A pause this long after speech ends the utterance.
constexpr int kUtterancePauseMs = 800;
// Speech that runs this long without a pause is ended anyway, under Codex's
// 30 s max_utterance_duration_ms. Counted in audio, as the service counts it:
// 16 kHz mono s16.
constexpr qsizetype kLongestUtteranceBytes = qsizetype(25) * 16000 * 2;

} // namespace

QString defaultRecordingPath(const QString &dataFolder, const QDateTime &startedAt)
{
    return QDir(dataFolder).filePath(
        QStringLiteral("recordings/%1.md").arg(startedAt.toString(QStringLiteral("yyyy-MM-dd-HHmm"))));
}

// Opened NewOnly, so an existing file is never written to even if one appears
// between the check and the open, and Unbuffered, so a line that could not
// be written is not written after all with the next.
bool RecordingTranscript::create(const QString &path, QString *error)
{
    const QFileInfo requested(path);
    if (!QDir().mkpath(requested.absolutePath())) {
        *error = recordingFileError(requested.absolutePath(), QStringLiteral("the folder could not be created"));
        return false;
    }
    const QString stem = requested.completeBaseName();
    const QString suffix = requested.suffix().isEmpty() ? QString() : QLatin1Char('.') + requested.suffix();
    for (int copy = 1;; ++copy) {
        const QString name = copy == 1 ? requested.fileName() : QStringLiteral("%1-%2%3").arg(stem).arg(copy).arg(suffix);
        m_file.setFileName(requested.dir().filePath(name));
        if (m_file.exists()) {
            continue;
        }
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Unbuffered)) {
            *error = recordingFileError(m_file.fileName(), m_file.errorString());
            return false;
        }
        return true;
    }
}

QString RecordingTranscript::path() const
{
    return m_file.fileName();
}

bool RecordingTranscript::append(qint64 elapsedMs, const QString &speaker, const QString &text, QString *error)
{
    const QByteArray line =
        QStringLiteral("[%1] %2: %3\n").arg(recordingClock(elapsedMs), speaker, text.simplified()).toUtf8();
    if (m_file.write(line) != line.size() || !m_file.flush()) {
        *error = recordingFileError(m_file.fileName(), m_file.errorString());
        return false;
    }
    return true;
}

void RecordingTranscript::close()
{
    m_file.close();
}

// The microphone as the recording's stream hears it: without what the
// speakers play when there is an echo canceller, and silent while a dictation
// has the microphone. The canceller takes all the microphone's audio either
// way, so it stays lined up with system audio.
class RecordingMicrophone final : public AudioInput {
public:
    RecordingMicrophone(AudioInput *microphone,
                        AudioInput *systemAudio,
                        std::unique_ptr<EchoCanceller> canceller,
                        QObject *parent)
        : AudioInput(parent)
        , m_microphone(microphone)
        , m_canceller(std::move(canceller))
    {
        m_microphone->setParent(this);
        connect(m_microphone, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
            deliver(m_canceller ? m_canceller->process(pcm) : pcm);
        });
        connect(m_microphone, &AudioInput::levelChanged, this, &AudioInput::levelChanged);
        connect(m_microphone, &AudioInput::failed, this, &AudioInput::failed);
        if (m_canceller && systemAudio) {
            connect(systemAudio, &AudioInput::audioChunk, this, [this](const QByteArray &pcm) {
                if (m_canceller) {
                    m_canceller->addReference(pcm);
                }
            });
        }
    }

    bool start(QString *error = nullptr) override { return m_microphone->start(error); }

    // The canceller gives back what it held, once, as nothing may go in after.
    void stop() override
    {
        m_microphone->stop();
        if (m_canceller) {
            deliver(std::exchange(m_canceller, nullptr)->flush());
        }
    }

    bool isActive() const override { return m_microphone->isActive(); }

    void setSilenced(bool silenced) { m_silenced = silenced; }

private:
    void deliver(const QByteArray &pcm)
    {
        if (!pcm.isEmpty()) {
            emit audioChunk(m_silenced ? QByteArray(pcm.size(), '\0') : pcm);
        }
    }

    AudioInput *m_microphone;
    std::unique_ptr<EchoCanceller> m_canceller;
    bool m_silenced = false;
};

RecordingSession::RecordingSession(SettingsStore *settings,
                                   ProviderRegistry *providers,
                                   InputFactory createMicrophone,
                                   InputFactory createSystemAudio,
                                   EchoCancellerFactory createEchoCanceller,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_createMicrophone(std::move(createMicrophone))
    , m_createSystemAudio(std::move(createSystemAudio))
    , m_createEchoCanceller(std::move(createEchoCanceller))
{
    qRegisterMetaType<RecordingStatus>();
    m_stopDeadline.setSingleShot(true);
    connect(&m_stopDeadline, &QTimer::timeout, this, [this] {
        for (Stream *stream : {&m_me, &m_them}) {
            if (stream->transcription->isRunning()) {
                stream->status.problem = recordingStopTimedOutText();
                stream->transcription->cancel();
            }
        }
    });
    connectStream(m_me);
    connectStream(m_them);
}

// The transcriptions stop the inputs as they go, so they go first, and say
// nothing more to this half-destroyed session.
RecordingSession::~RecordingSession()
{
    for (Stream *stream : {&m_me, &m_them}) {
        disconnect(stream->transcription, nullptr, this, nullptr);
        delete stream->transcription;
    }
    delete m_microphone;
    delete m_them.input;
}

void RecordingSession::connectStream(Stream &stream)
{
    stream.transcription = new FileTranscriptionSession(m_settings, m_providers, this);
    stream.pauseTimer.setSingleShot(true);
    stream.pauseTimer.setInterval(kUtterancePauseMs);
    connect(&stream.pauseTimer, &QTimer::timeout, this, [this, &stream] { endUtterance(stream); });
    connect(stream.transcription, &FileTranscriptionSession::speechConnected, this,
            [this, &stream] { handleStreamConnected(stream); });
    connect(stream.transcription, &FileTranscriptionSession::speechReconnecting, this,
            [this, &stream](int, const QString &reason) {
                stream.status.state = RecordingStream::State::Reconnecting;
                stream.status.problem = reason;
                emit problemChanged();
            });
    connect(stream.transcription, &FileTranscriptionSession::microphoneAudioLost, this,
            [this, &stream](int, qint64 durationMs) {
                const bool firstLoss = stream.status.lostAudioMs == 0;
                stream.status.lostAudioMs += durationMs;
                if (firstLoss) {
                    emit problemChanged();
                }
            });
    connect(stream.transcription, &FileTranscriptionSession::utteranceFailed, this,
            [this, &stream](int, const QString &reason) {
                stream.status.problem = recordingUtteranceFailedText(reason);
                emit problemChanged();
                extendStop();
            });
    connect(stream.transcription, &FileTranscriptionSession::fileTextFinalized, this,
            [this, &stream](int, const QString &text) {
                writeLine(stream.status.speaker, text);
                extendStop();
            });
    connect(stream.transcription, &FileTranscriptionSession::batchFinished, this,
            [this, &stream](const QList<TranscribeFileResult> &results) {
                handleTranscriptionFinished(stream, results);
            });
}

bool RecordingSession::isRecording() const
{
    return m_phase == Phase::Recording || m_phase == Phase::Stopping;
}

RecordingStatus RecordingSession::status() const
{
    if (!isRecording()) {
        return {};
    }
    QList<RecordingStream> streams{m_me.status};
    if (m_them.used) {
        streams << m_them.status;
    }
    return {true, m_transcript.path(), m_clock.elapsed(), streams, m_unwrittenLines, m_writeError,
            m_echoCancellationWarning};
}

void RecordingSession::start(const QString &path,
                             const QStringList &vocabulary,
                             const QString &dataFolder,
                             bool microphoneOnly,
                             StartDone done)
{
    if (m_phase != Phase::Off) {
        done(alreadyRecordingText(m_transcript.path()));
        return;
    }
    const SpeechSettings speech = m_settings->snapshot().speech;
    const SpeechTranscriber *provider = m_providers->speechProvider(speech.providerId);
    // Local Models finalize runs of words rather than utterances, and their
    // tail only in a whole-attempt transcript, so they say no.
    if (!provider || !provider->streamsFinalText(speech)) {
        done(recordingSpeechProviderRefusal(m_providers->speechProviderLabel(speech.providerId)));
        return;
    }
    QString error;
    if (!m_transcript.create(path.isEmpty() ? defaultRecordingPath(dataFolder, QDateTime::currentDateTime()) : path,
                             &error)) {
        done(error);
        return;
    }
    TranscribeOptions options;
    options.speechProviderId = speech.providerId;
    options.streamedFinalsOnly = true;
    options.addedVocabulary = vocabulary;
    m_me.status = {recordingMicrophoneSpeaker()};
    m_them.status = {recordingSystemAudioSpeaker()};
    m_unwrittenLines = 0;
    m_writeError.clear();
    m_echoCancellationWarning.clear();
    AudioInput *systemAudio = !microphoneOnly && m_createSystemAudio ? m_createSystemAudio(this) : nullptr;
    std::unique_ptr<EchoCanceller> canceller;
    if (systemAudio && m_createEchoCanceller) {
        canceller = m_createEchoCanceller(&m_echoCancellationWarning);
    }
    m_microphone = new RecordingMicrophone(m_createMicrophone(this), systemAudio, std::move(canceller), this);
    m_microphone->setSilenced(m_dictating);
    m_me.input = m_microphone;
    m_them.input = systemAudio;
    m_me.used = true;
    m_them.used = systemAudio != nullptr;
    m_me.connecting = true;
    m_them.connecting = m_them.used;
    m_voiceThreshold = m_settings->audioCaptureSettings().vadThresholdPercent;
    m_phase = Phase::Starting;
    m_startDone = std::move(done);
    m_clock.start();
    startStream(m_me, options);
    // A microphone that cannot start has failed the start already.
    if (m_phase == Phase::Off) {
        return;
    }
    if (m_them.used) {
        startStream(m_them, options);
    }
}

void RecordingSession::startStream(Stream &stream, const TranscribeOptions &options)
{
    stream.utteranceBytes = 0;
    stream.transcription->startListening(stream.input, options);
    // After the transcription's own connection, so the audio that ends an
    // utterance is already queued for the provider. An input that could not
    // start has finished its stream already.
    if (stream.transcription->isRunning()) {
        connect(stream.input, &AudioInput::audioChunk, this,
                [this, &stream](const QByteArray &pcm) { trackUtterance(stream, pcm); });
    }
}

void RecordingSession::stop()
{
    if (m_phase != Phase::Recording) {
        return;
    }
    m_phase = Phase::Stopping;
    QList<Stream *> running;
    for (Stream *stream : {&m_me, &m_them}) {
        if (stream->transcription->isRunning()) {
            running << stream;
        }
    }
    if (running.isEmpty()) {
        m_me.pauseTimer.stop();
        m_them.pauseTimer.stop();
        finish();
        return;
    }
    m_stopLimit.setRemainingTime(kLongestRecordingStopMs);
    m_utteranceAnswerTimeoutMs = 0;
    for (Stream *stream : running) {
        m_utteranceAnswerTimeoutMs =
            std::max(m_utteranceAnswerTimeoutMs, stream->transcription->utteranceAnswerTimeoutMs());
    }
    m_stopDeadline.start(m_utteranceAnswerTimeoutMs);
    for (Stream *stream : running) {
        const bool speaking = stream->pauseTimer.isActive();
        stream->pauseTimer.stop();
        stream->transcription->finishListening();
        // The utterance being spoken ends here, as the others did at a
        // pause, once the input has delivered its post-roll: a provider that
        // transcribes utterances does not take the audio after the last.
        if (speaking) {
            endUtterance(*stream);
        }
    }
}

void RecordingSession::discard()
{
    if (m_phase != Phase::Recording) {
        return;
    }
    const QString path = m_transcript.path();
    m_phase = Phase::Stopping;
    m_me.pauseTimer.stop();
    m_them.pauseTimer.stop();
    // Cancelling finishes the transcriptions, the last of which finishes the
    // recording.
    if (m_me.transcription->isRunning() || m_them.transcription->isRunning()) {
        m_me.transcription->cancel();
        m_them.transcription->cancel();
    } else {
        finish();
    }
    QFile::remove(path);
}

void RecordingSession::setDictating(bool dictating)
{
    if (dictating == m_dictating) {
        return;
    }
    m_dictating = dictating;
    if (m_microphone) {
        m_microphone->setSilenced(dictating);
    }
    if (m_phase != Phase::Recording) {
        return;
    }
    if (dictating) {
        writeLine(m_me.status.speaker, recordingDictatingText());
    }
    if (m_me.status.state == RecordingStream::State::Recording
        || m_me.status.state == RecordingStream::State::Paused) {
        m_me.status.state = liveState(m_me);
        emit problemChanged();
    }
}

// The first connection settles the stream's start; a later one ends a
// reconnect.
void RecordingSession::handleStreamConnected(Stream &stream)
{
    stream.status.state = liveState(stream);
    stream.status.problem.clear();
    if (m_phase != Phase::Starting) {
        emit problemChanged();
        return;
    }
    stream.connecting = false;
    finishStartOnceSettled();
}

void RecordingSession::finishStartOnceSettled()
{
    if (m_phase != Phase::Starting || m_me.connecting || m_them.connecting) {
        return;
    }
    m_phase = Phase::Recording;
    qInfo().noquote() << "recording started path=" + m_transcript.path();
    if (m_dictating) {
        writeLine(m_me.status.speaker, recordingDictatingText());
    }
    emit recordingChanged(true);
    std::exchange(m_startDone, {})({});
}

// Skip silence holds quiet audio back, so a pause is timed by the clock
// rather than by the audio that arrives. Voice with no utterance open begins
// one.
void RecordingSession::trackUtterance(Stream &stream, const QByteArray &pcm)
{
    if (m_phase == Phase::Stopping) {
        return;
    }
    if (isVoiced(rmsForPcm16(pcm), m_voiceThreshold)) {
        if (!stream.pauseTimer.isActive()) {
            stream.transcription->beginUtterance(pcm.size());
        }
        stream.pauseTimer.start();
    }
    if (!stream.pauseTimer.isActive()) {
        return;
    }
    stream.utteranceBytes += pcm.size();
    if (stream.utteranceBytes >= kLongestUtteranceBytes) {
        endUtterance(stream);
    }
}

void RecordingSession::endUtterance(Stream &stream)
{
    stream.pauseTimer.stop();
    stream.utteranceBytes = 0;
    stream.transcription->endUtterance();
}

// While a stop waits, each utterance the provider answers gives it time for
// the next, up to kLongestRecordingStopMs in all.
void RecordingSession::extendStop()
{
    if (m_stopDeadline.isActive()) {
        m_stopDeadline.start(int(std::min<qint64>(m_utteranceAnswerTimeoutMs, m_stopLimit.remainingTime())));
    }
}

void RecordingSession::writeLine(const QString &speaker, const QString &text)
{
    if (m_phase == Phase::Off || text.simplified().isEmpty()) {
        return;
    }
    QString error;
    if (!m_transcript.append(m_clock.elapsed(), speaker, text, &error)) {
        qWarning().noquote() << "recording could not write a line: " + error;
        if (m_unwrittenLines++ == 0) {
            m_writeError = error;
            emit problemChanged();
        }
    }
}

// Before a stop, the stream ended on its own: the provider failed in a way a
// reconnect cannot mend, or the input went away. The microphone's failing to
// connect fails the start; otherwise the recording stays open, with the other
// stream if it runs, so status says why, until it is stopped.
void RecordingSession::handleTranscriptionFinished(Stream &stream, const QList<TranscribeFileResult> &results)
{
    if (m_phase == Phase::Off) {
        return;
    }
    const QString error = results.isEmpty() ? QString() : results.first().error;
    if (m_phase == Phase::Starting && &stream == &m_me) {
        abandon(error.isEmpty() ? recordingStreamEndedText() : error);
        return;
    }
    stream.status.state = RecordingStream::State::Stopped;
    if (m_phase == Phase::Stopping) {
        if (!error.isEmpty()) {
            stream.status.problem = error;
        }
        if (!m_me.transcription->isRunning() && !m_them.transcription->isRunning()) {
            finish();
        }
        return;
    }
    stream.status.problem = error.isEmpty() ? recordingStreamEndedText() : error;
    qWarning().noquote() << "recording stream " + stream.status.speaker + " stopped: " + stream.status.problem;
    if (m_phase == Phase::Starting) {
        stream.connecting = false;
        finishStartOnceSettled();
        return;
    }
    emit problemChanged();
}

RecordingStream::State RecordingSession::liveState(const Stream &stream) const
{
    return &stream == &m_me && m_dictating ? RecordingStream::State::Paused : RecordingStream::State::Recording;
}

void RecordingSession::releaseInputs()
{
    if (m_microphone) {
        m_microphone->deleteLater();
    }
    if (m_them.input) {
        m_them.input->deleteLater();
    }
}

// The microphone's stream never connected, so nothing was recorded and
// neither is the file.
void RecordingSession::abandon(const QString &error)
{
    m_phase = Phase::Off;
    m_them.transcription->cancel();
    m_transcript.close();
    QFile::remove(m_transcript.path());
    releaseInputs();
    qWarning().noquote() << "recording could not start: " + error;
    std::exchange(m_startDone, {})(error);
}

void RecordingSession::finish()
{
    m_stopDeadline.stop();
    const RecordingStatus last = status();
    m_transcript.close();
    releaseInputs();
    m_phase = Phase::Off;
    qInfo().noquote() << "recording stopped path=" + last.path;
    emit recordingChanged(false);
    emit stopped(last);
}

} // namespace speecher
