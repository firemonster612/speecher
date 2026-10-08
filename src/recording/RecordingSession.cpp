#include "recording/RecordingSession.h"

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

// Who the microphone's lines name; system audio's will be "them".
const QString kMicrophoneSpeaker = QStringLiteral("me");
// How long a stop waits for the provider to finish each utterance left, up
// to kLongestRecordingStopMs in all.
constexpr int kStopTimeoutMs = 15000;
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

RecordingSession::RecordingSession(SettingsStore *settings,
                                   ProviderRegistry *providers,
                                   MicrophoneFactory createMicrophone,
                                   QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_createMicrophone(std::move(createMicrophone))
    , m_transcription(new FileTranscriptionSession(settings, providers, this))
{
    qRegisterMetaType<RecordingStatus>();
    m_stopDeadline.setSingleShot(true);
    connect(&m_stopDeadline, &QTimer::timeout, this, [this] {
        m_stream.problem = recordingStopTimedOutText();
        m_transcription->cancel();
    });
    m_pauseTimer.setSingleShot(true);
    m_pauseTimer.setInterval(kUtterancePauseMs);
    connect(&m_pauseTimer, &QTimer::timeout, this, &RecordingSession::endUtterance);
    connect(m_transcription, &FileTranscriptionSession::speechConnected, this,
            &RecordingSession::handleStreamConnected);
    connect(m_transcription, &FileTranscriptionSession::speechReconnecting, this,
            [this](int, const QString &reason) {
                m_stream.state = RecordingStream::State::Reconnecting;
                m_stream.problem = reason;
            });
    connect(m_transcription, &FileTranscriptionSession::microphoneAudioLost, this,
            [this](int, qint64 durationMs) { m_stream.lostAudioMs += durationMs; });
    connect(m_transcription, &FileTranscriptionSession::utteranceFailed, this, [this](int, const QString &reason) {
        m_stream.problem = recordingUtteranceFailedText(reason);
        extendStop();
    });
    connect(m_transcription, &FileTranscriptionSession::fileTextFinalized, this, [this](int, const QString &text) {
        writeLine(text);
        extendStop();
    });
    connect(m_transcription, &FileTranscriptionSession::batchFinished, this,
            &RecordingSession::handleTranscriptionFinished);
}

// The transcription stops the microphone as it goes, so it goes first, and
// says nothing more to this half-destroyed session.
RecordingSession::~RecordingSession()
{
    disconnect(m_transcription, nullptr, this, nullptr);
    delete m_transcription;
    delete m_microphone;
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
    return {true, m_transcript.path(), m_clock.elapsed(), {m_stream}, m_unwrittenLines, m_writeError};
}

void RecordingSession::start(const QString &path,
                             const QStringList &vocabulary,
                             const QString &dataFolder,
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
    m_stream = {kMicrophoneSpeaker};
    m_unwrittenLines = 0;
    m_writeError.clear();
    m_phase = Phase::Starting;
    m_startDone = std::move(done);
    m_microphone = m_createMicrophone(this);
    m_clock.start();
    m_transcription->startListening(m_microphone, options);
    // A microphone that cannot start has failed the start already.
    if (m_phase == Phase::Off) {
        return;
    }
    // After the transcription's own connection, so the audio that ends an
    // utterance is already queued for the provider.
    m_voiceThreshold = m_settings->audioCaptureSettings().vadThresholdPercent;
    m_utteranceBytes = 0;
    connect(m_microphone, &AudioInput::audioChunk, this, &RecordingSession::trackUtterance);
}

void RecordingSession::stop()
{
    if (m_phase != Phase::Recording) {
        return;
    }
    m_phase = Phase::Stopping;
    const bool speaking = m_pauseTimer.isActive();
    m_pauseTimer.stop();
    if (!m_transcription->isRunning()) {
        finish();
        return;
    }
    m_stopLimit.setRemainingTime(kLongestRecordingStopMs);
    m_stopDeadline.start(kStopTimeoutMs);
    m_transcription->finishListening();
    // The utterance being spoken ends here, as the others did at a pause,
    // once the microphone has delivered its post-roll: a provider that
    // transcribes utterances does not take the audio after the last.
    if (speaking) {
        endUtterance();
    }
}

void RecordingSession::discard()
{
    if (m_phase != Phase::Recording) {
        return;
    }
    const QString path = m_transcript.path();
    m_phase = Phase::Stopping;
    m_pauseTimer.stop();
    // Cancelling finishes the transcription, which finishes the recording.
    if (m_transcription->isRunning()) {
        m_transcription->cancel();
    } else {
        finish();
    }
    QFile::remove(path);
}

// The first connection starts the recording; a later one ends a reconnect.
void RecordingSession::handleStreamConnected()
{
    m_stream.state = RecordingStream::State::Recording;
    m_stream.problem.clear();
    if (m_phase != Phase::Starting) {
        return;
    }
    m_phase = Phase::Recording;
    qInfo().noquote() << "recording started path=" + m_transcript.path();
    emit recordingChanged(true);
    std::exchange(m_startDone, {})({});
}

// Skip silence holds quiet audio back, so a pause is timed by the clock
// rather than by the audio that arrives. Voice with no utterance open begins
// one.
void RecordingSession::trackUtterance(const QByteArray &pcm)
{
    if (m_phase == Phase::Stopping) {
        return;
    }
    if (isVoiced(rmsForPcm16(pcm), m_voiceThreshold)) {
        if (!m_pauseTimer.isActive()) {
            m_transcription->beginUtterance(pcm.size());
        }
        m_pauseTimer.start();
    }
    if (!m_pauseTimer.isActive()) {
        return;
    }
    m_utteranceBytes += pcm.size();
    if (m_utteranceBytes >= kLongestUtteranceBytes) {
        endUtterance();
    }
}

void RecordingSession::endUtterance()
{
    m_pauseTimer.stop();
    m_utteranceBytes = 0;
    m_transcription->endUtterance();
}

// While a stop waits, each utterance the provider answers gives it time for
// the next.
void RecordingSession::extendStop()
{
    if (m_stopDeadline.isActive()) {
        m_stopDeadline.start(int(std::min<qint64>(kStopTimeoutMs, m_stopLimit.remainingTime())));
    }
}

void RecordingSession::writeLine(const QString &text)
{
    if (m_phase == Phase::Off || text.simplified().isEmpty()) {
        return;
    }
    QString error;
    if (!m_transcript.append(m_clock.elapsed(), m_stream.speaker, text, &error)) {
        qWarning().noquote() << "recording could not write a line: " + error;
        if (m_unwrittenLines++ == 0) {
            m_writeError = error;
        }
    }
}

// Before a stop, the stream ended on its own: the provider failed in a way a
// reconnect cannot mend, or the microphone went away. The recording stays
// open, so status says why, until it is stopped.
void RecordingSession::handleTranscriptionFinished(const QList<TranscribeFileResult> &results)
{
    const QString error = results.isEmpty() ? QString() : results.first().error;
    if (m_phase == Phase::Starting) {
        abandon(error.isEmpty() ? recordingStreamEndedText() : error);
        return;
    }
    m_stream.state = RecordingStream::State::Stopped;
    if (m_phase == Phase::Stopping) {
        if (!error.isEmpty()) {
            m_stream.problem = error;
        }
        finish();
        return;
    }
    m_stream.problem = error.isEmpty() ? recordingStreamEndedText() : error;
    qWarning().noquote() << "recording stream stopped: " + m_stream.problem;
}

// No stream connected, so nothing was recorded and neither is the file.
void RecordingSession::abandon(const QString &error)
{
    m_transcript.close();
    QFile::remove(m_transcript.path());
    if (m_microphone) {
        m_microphone->deleteLater();
    }
    m_phase = Phase::Off;
    qWarning().noquote() << "recording could not start: " + error;
    std::exchange(m_startDone, {})(error);
}

void RecordingSession::finish()
{
    m_stopDeadline.stop();
    const RecordingStatus last = status();
    m_transcript.close();
    if (m_microphone) {
        m_microphone->deleteLater();
    }
    m_phase = Phase::Off;
    qInfo().noquote() << "recording stopped path=" + last.path;
    emit recordingChanged(false);
    emit stopped(last);
}

} // namespace speecher
