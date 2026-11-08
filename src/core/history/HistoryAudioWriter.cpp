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

// A canonical 44-byte header for dataBytes of 16 kHz mono 16-bit PCM.
QByteArray wavHeader(quint32 dataBytes)
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
    put16(1, 22);
    put32(sampleRate, 24);
    put32(sampleRate * bytesPerSample, 28);
    put16(bytesPerSample, 32);
    put16(16, 34);
    memcpy(at + 36, "data", 4);
    put32(dataBytes, 40);
    return header;
}

} // namespace

QAudioFormat HistoryAudioWriter::pcmFormat()
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

HistoryAudioWriter::HistoryAudioWriter(const QString &folder, QObject *parent)
    : QObject(parent)
    , m_folder(folder)
    , m_encoded(encodesAac())
{
    // A voice is as private as the words: the folder is the owner's alone.
    QDir().mkpath(folder);
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    m_name = QUuid::createUuid().toString(QUuid::WithoutBraces) + (m_encoded ? QStringLiteral(".m4a")
                                                                              : QStringLiteral(".wav"));
    if (!m_encoded) {
        m_wav = new QFile(partPath(), this);
        if (!m_wav->open(QIODevice::WriteOnly) || m_wav->write(wavHeader(0)) != 44) {
            qWarning().noquote() << "history audio could not be written path=" + partPath() << m_wav->errorString();
        }
        m_wav->setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        return;
    }
    m_session = new QMediaCaptureSession(this);
    m_input = new QAudioBufferInput(pcmFormat(), this);
    m_recorder = new QMediaRecorder(this);
    m_session->setAudioBufferInput(m_input);
    m_session->setRecorder(m_recorder);
    QMediaFormat format(QMediaFormat::MPEG4);
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_recorder->setMediaFormat(format);
    m_recorder->setAudioBitRate(aacBitRate);
    m_recorder->setAudioChannelCount(1);
    m_recorder->setAudioSampleRate(sampleRate);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(partPath()));
    // The empty buffer finish() sends ends the recording.
    m_recorder->setAutoStop(true);
    connect(m_input, &QAudioBufferInput::readyToSendAudioBuffer, this, &HistoryAudioWriter::sendPending);
    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState && m_finishing) {
            finishEncoding(true);
        }
    });
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &message) {
        qWarning().noquote() << "history audio could not be encoded:" << message;
        finishEncoding(false);
    });
    m_recorder->record();
}

HistoryAudioWriter::~HistoryAudioWriter()
{
    if (m_recorder) {
        m_recorder->stop();
    }
    // Whatever didn't get its name was never finished.
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
    m_samples += pcm.size() / bytesPerSample;
    if (m_wav) {
        m_wav->write(pcm);
        return;
    }
    m_pending.append(pcm);
    sendPending();
}

// Hands the encoder what it can take now; the rest waits for it to be ready.
void HistoryAudioWriter::sendPending()
{
    while (!m_pending.isEmpty()) {
        const QAudioBuffer buffer(m_pending.first(), pcmFormat(), m_samplesSent * 1000000 / sampleRate);
        if (!m_input->sendAudioBuffer(buffer)) {
            return;
        }
        m_samplesSent += m_pending.takeFirst().size() / bytesPerSample;
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
    if (m_samples == 0) {
        discard();
        return {};
    }
    m_finishing = true;
    if (!m_wav) {
        sendPending();
        return m_name;
    }
    const bool written = m_wav->error() == QFileDevice::NoError && m_wav->seek(0)
        && m_wav->write(wavHeader(quint32(m_samples * bytesPerSample))) == 44;
    m_wav->close();
    finishEncoding(written);
    return written ? m_name : QString();
}

void HistoryAudioWriter::finishEncoding(bool ok)
{
    if (m_discarded) {
        return;
    }
    m_discarded = true;
    const QString path = QDir(m_folder).filePath(m_name);
    if (ok && !QFile::rename(partPath(), path)) {
        qWarning().noquote() << "history audio could not be named path=" + path;
    }
    deleteLater();
}

void HistoryAudioWriter::discard()
{
    m_discarded = true;
    deleteLater();
}

} // namespace speecher
