#include "core/history/HistoryAudioWriter.h"

#include <QAudioBuffer>
#include <QAudioBufferInput>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

namespace speecher {
namespace {

constexpr int sampleRate = 16000;
constexpr int bytesPerSample = 2;
constexpr int aacBitRate = 32000;

// Whether Qt's media backend here encodes AAC into MP4; asked once, since the
// answer can't change while Speecher runs.
bool encodesAac()
{
    static const bool encodes = [] {
        QMediaFormat format(QMediaFormat::MPEG4);
        format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
        return format.isSupported(QMediaFormat::Encode);
    }();
    return encodes;
}

// A canonical 44-byte header for dataBytes of 16 kHz 16-bit PCM.
QByteArray wavHeader(quint32 dataBytes, quint16 channels)
{
    QByteArray header(44, '\0');
    char *at = header.data();
    const auto put32 = [&at](quint32 value, int offset) { qToLittleEndian(value, at + offset); };
    const auto put16 = [&at](quint16 value, int offset) { qToLittleEndian(value, at + offset); };
    memcpy(at, "RIFF", 4);
    put32(36 + dataBytes, 4);
    memcpy(at + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(1, 20);
    put16(channels, 22);
    put32(sampleRate, 24);
    put32(sampleRate * bytesPerSample * channels, 28);
    put16(bytesPerSample * channels, 32);
    put16(16, 34);
    memcpy(at + 36, "data", 4);
    put32(dataBytes, 40);
    return header;
}

// A voice is as private as the words: the folder, made where it is
// missing, is the owner's alone.
QString ownersOnly(const QString &folder)
{
    QDir().mkpath(folder);
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return folder;
}

} // namespace

QAudioFormat HistoryAudioWriter::pcmFormat(int channels)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

HistoryAudioWriter::HistoryAudioWriter(const QString &folder, QObject *parent)
    : HistoryAudioWriter(ownersOnly(folder), QUuid::createUuid().toString(QUuid::WithoutBraces), 1, parent)
{
}

HistoryAudioWriter::HistoryAudioWriter(const QString &folder, const QString &stem, int channels, QObject *parent)
    : QObject(parent)
    , m_folder(folder)
    , m_channels(channels)
    , m_encoded(encodesAac())
{
    QDir().mkpath(folder);
    m_name = stem + (m_encoded ? QStringLiteral(".m4a") : QStringLiteral(".wav"));
    if (!m_encoded) {
        m_wav = new QFile(partPath(), this);
        if (!m_wav->open(QIODevice::WriteOnly) || m_wav->write(wavHeader(0, quint16(m_channels))) != 44) {
            m_error = m_wav->errorString();
            qWarning().noquote() << "history audio could not be written path=" + partPath() << m_error;
        }
        m_wav->setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        return;
    }
    m_session = new QMediaCaptureSession(this);
    m_input = new QAudioBufferInput(pcmFormat(m_channels), this);
    m_recorder = new QMediaRecorder(this);
    m_session->setAudioBufferInput(m_input);
    m_session->setRecorder(m_recorder);
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_recorder->setMediaFormat(format);
    m_recorder->setAudioBitRate(aacBitRate);
    m_recorder->setAudioChannelCount(m_channels);
    m_recorder->setAudioSampleRate(sampleRate);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(partPath()));
    // The empty buffer finish() sends ends the recording.
    m_recorder->setAutoStop(true);
    connect(m_input, &QAudioBufferInput::readyToSendAudioBuffer, this, &HistoryAudioWriter::sendPending);
    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && m_finishing) {
            finishEncoding({});
        }
    });
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &message) {
        qWarning().noquote() << "history audio could not be encoded:" << message;
        finishEncoding(message.isEmpty() ? QStringLiteral("the encoder failed") : message);
    });
    m_recorder->record();
}

HistoryAudioWriter::~HistoryAudioWriter()
{
    if (m_recorder) {
        m_recorder->stop();
    }
    // Closed first: Windows won't delete a file still open. Whatever didn't
    // get its name was never finished.
    if (m_wav) {
        m_wav->close();
    }
    QFile::remove(partPath());
}

QString HistoryAudioWriter::partPath() const
{
    return QDir(m_folder).filePath(m_name + QStringLiteral(".part"));
}

void HistoryAudioWriter::write(const QByteArray &pcm)
{
    if (m_finishing || m_discarded || pcm.isEmpty()) {
        return;
    }
    m_samples += pcm.size() / (bytesPerSample * m_channels);
    if (m_wav) {
        if (m_wav->write(pcm) != pcm.size() && m_error.isEmpty()) {
            m_error = m_wav->errorString();
        }
        return;
    }
    m_pending.append(pcm);
    sendPending();
}

// Hands the encoder what it can take now; the rest waits for it to be ready.
void HistoryAudioWriter::sendPending()
{
    while (!m_pending.isEmpty()) {
        const QAudioBuffer buffer(m_pending.first(), pcmFormat(m_channels), m_samplesSent * 1000000 / sampleRate);
        if (!m_input->sendAudioBuffer(buffer)) {
            return;
        }
        m_samplesSent += m_pending.takeFirst().size() / (bytesPerSample * m_channels);
    }
    if (m_finishing && !m_endSent) {
        m_endSent = m_input->sendAudioBuffer({});
    }
}

QString HistoryAudioWriter::finish()
{
    if (m_finishing || m_discarded) {
        return {};
    }
    m_finishing = true;
    if (m_samples == 0) {
        finishEncoding({});
        return {};
    }
    if (!m_wav) {
        sendPending();
        return m_name;
    }
    if (m_error.isEmpty()
        && (!m_wav->seek(0)
            || m_wav->write(wavHeader(quint32(m_samples * bytesPerSample * m_channels), quint16(m_channels))) != 44)) {
        m_error = m_wav->errorString();
    }
    m_wav->close();
    finishEncoding(m_error);
    return m_error.isEmpty() ? m_name : QString();
}

// Names the file, unless error says why it can't be, then says so once and
// goes. Nothing heard is no file and no error.
void HistoryAudioWriter::finishEncoding(const QString &error)
{
    if (m_discarded) {
        return;
    }
    m_discarded = true;
    QString name = m_samples > 0 && error.isEmpty() ? m_name : QString();
    QString failure = m_samples > 0 ? error : QString();
    const QString path = QDir(m_folder).filePath(m_name);
    if (!name.isEmpty() && !QFile::rename(partPath(), path)) {
        failure = QStringLiteral("it couldn't be saved as ") + path;
        qWarning().noquote() << "history audio could not be named path=" + path;
        name.clear();
    }
    emit finished(name, failure);
    deleteLater();
}

void HistoryAudioWriter::discard()
{
    m_discarded = true;
    // The encoder lets go of its file only once it has stopped, and Windows
    // won't delete a file still open, so the writer goes after that.
    if (m_recorder && m_recorder->recorderState() != QMediaRecorder::StoppedState) {
        connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::StoppedState) {
                deleteLater();
            }
        });
        connect(m_recorder, &QMediaRecorder::errorOccurred, this, &QObject::deleteLater);
        m_recorder->stop();
        return;
    }
    deleteLater();
}

} // namespace speecher
