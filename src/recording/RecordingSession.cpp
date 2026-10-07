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
#include <QJsonArray>

namespace speecher {
namespace {

// Who the microphone's lines name; system audio's will be "them".
const QString kMicrophoneSpeaker = QStringLiteral("me");
// How long a stop waits for the provider to finish the last utterance.
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
// between the check and the open.
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
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
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

QJsonObject recordingStatusJson(const RecordingStatus &status)
{
    if (!status.recording) {
        return {{QStringLiteral("recording"), false}};
    }
    QJsonArray streams;
    for (const RecordingStream &stream : status.streams) {
        QJsonObject object{{QStringLiteral("speaker"), stream.speaker},
                           {QStringLiteral("state"), stream.running ? QStringLiteral("recording")
                                                                    : QStringLiteral("stopped")}};
        if (!stream.problem.isEmpty()) {
            object.insert(QStringLiteral("problem"), stream.problem);
        }
        streams.append(object);
    }
    return {{QStringLiteral("recording"), true},
            {QStringLiteral("path"), status.path},
            {QStringLiteral("durationMs"), status.durationMs},
            {QStringLiteral("streams"), streams}};
}

RecordingStatus recordingStatusFromJson(const QJsonObject &object)
{
    RecordingStatus status;
    status.recording = object.value(QStringLiteral("recording")).toBool();
    status.path = object.value(QStringLiteral("path")).toString();
    status.durationMs = object.value(QStringLiteral("durationMs")).toInteger();
    for (const QJsonValue &value : object.value(QStringLiteral("streams")).toArray()) {
        const QJsonObject stream = value.toObject();
        status.streams.append({stream.value(QStringLiteral("speaker")).toString(),
                               stream.value(QStringLiteral("state")).toString() == QStringLiteral("recording"),
                               stream.value(QStringLiteral("problem")).toString()});
    }
    return status;
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
    m_stopDeadline.setInterval(kStopTimeoutMs);
    connect(&m_stopDeadline, &QTimer::timeout, m_transcription, &FileTranscriptionSession::cancel);
    m_pauseTimer.setSingleShot(true);
    m_pauseTimer.setInterval(kUtterancePauseMs);
    connect(&m_pauseTimer, &QTimer::timeout, this, &RecordingSession::endUtterance);
    connect(m_transcription, &FileTranscriptionSession::fileTextFinalized, this,
            [this](int, const QString &text) { writeLine(text); });
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
    return m_recording;
}

RecordingStatus RecordingSession::status() const
{
    if (!m_recording) {
        return {};
    }
    return {true, m_transcript.path(), m_clock.elapsed(), {m_stream}};
}

QString RecordingSession::start(const QString &path, const QString &dataFolder, QString *error)
{
    if (m_recording) {
        *error = alreadyRecordingText(m_transcript.path());
        return {};
    }
    const SpeechSettings speech = m_settings->snapshot().speech;
    const SpeechTranscriber *provider = m_providers->speechProvider(speech.providerId);
    // A batch provider would need the audio cut into chunks at its silences,
    // which recording does not do yet.
    if (!provider || !provider->streamsFinalText(speech)) {
        *error = batchSpeechProviderRefusal(m_providers->speechProviderLabel(speech.providerId));
        return {};
    }
    if (!m_transcript.create(path.isEmpty() ? defaultRecordingPath(dataFolder, QDateTime::currentDateTime()) : path,
                             error)) {
        return {};
    }
    TranscribeOptions options;
    options.speechProviderId = speech.providerId;
    options.streamedFinalsOnly = true;
    m_stream = {kMicrophoneSpeaker, true, {}};
    m_recording = true;
    m_stopping = false;
    m_microphone = m_createMicrophone(this);
    m_clock.start();
    m_transcription->startListening(m_microphone, options);
    // A microphone that cannot start fails the stream at once: nothing was
    // recorded, so neither is the file.
    if (!m_transcription->isRunning()) {
        *error = m_stream.problem;
        m_recording = false;
        m_transcript.close();
        QFile::remove(m_transcript.path());
        delete m_microphone;
        return {};
    }
    // After the transcription's own connection, so the audio that ends an
    // utterance is already queued for the provider.
    m_voiceThreshold = m_settings->audioCaptureSettings().vadThresholdPercent;
    m_utteranceBytes = 0;
    connect(m_microphone, &AudioInput::audioChunk, this, &RecordingSession::trackUtterance);
    qInfo().noquote() << "recording started path=" + m_transcript.path();
    emit recordingChanged(true);
    return m_transcript.path();
}

void RecordingSession::stop()
{
    if (!m_recording || m_stopping) {
        return;
    }
    m_stopping = true;
    m_pauseTimer.stop();
    if (!m_transcription->isRunning()) {
        finish();
        return;
    }
    m_stopDeadline.start();
    m_transcription->finishListening();
}

// Skip silence holds quiet audio back, so a pause is timed by the clock
// rather than by the audio that arrives.
void RecordingSession::trackUtterance(const QByteArray &pcm)
{
    if (m_stopping) {
        return;
    }
    if (isVoiced(rmsForPcm16(pcm), m_voiceThreshold)) {
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

void RecordingSession::writeLine(const QString &text)
{
    if (!m_recording || text.simplified().isEmpty()) {
        return;
    }
    QString error;
    if (!m_transcript.append(m_clock.elapsed(), m_stream.speaker, text, &error)) {
        qWarning().noquote() << "recording could not write a line: " + error;
        m_stream.problem = error;
    }
}

// Before a stop, the stream ended on its own: the provider failed past its
// reconnects, or the microphone went away. The recording stays open, so
// status says why, until it is stopped.
void RecordingSession::handleTranscriptionFinished(const QList<TranscribeFileResult> &results)
{
    m_stream.running = false;
    if (m_stopping) {
        finish();
        return;
    }
    const QString error = results.isEmpty() ? QString() : results.first().error;
    m_stream.problem = error.isEmpty() ? recordingStreamEndedText() : error;
    qWarning().noquote() << "recording stream stopped: " + m_stream.problem;
}

void RecordingSession::finish()
{
    m_stopDeadline.stop();
    const RecordingStatus last = status();
    m_transcript.close();
    if (m_microphone) {
        m_microphone->deleteLater();
    }
    m_recording = false;
    m_stopping = false;
    qInfo().noquote() << "recording stopped path=" + last.path;
    emit recordingChanged(false);
    emit stopped(last);
}

} // namespace speecher
