#include "core/AudioCapture.h"

#include <QAudioDevice>
#include <QMediaDevices>

#include <cmath>

namespace speecher {

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
}

bool AudioCapture::start(QString *error)
{
    stop();

    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        if (error) {
            *error = QStringLiteral("No audio input device available");
        }
        return false;
    }
    if (!device.isFormatSupported(format)) {
        format = device.preferredFormat();
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
    }

    m_source.reset(new QAudioSource(device, format, this));
    m_source->setBufferSize(4096);
    m_device = m_source->start();
    if (!m_device) {
        if (error) {
            *error = QStringLiteral("Could not start audio capture");
        }
        m_source.reset();
        return false;
    }
    connect(m_device, &QIODevice::readyRead, this, &AudioCapture::onReadyRead);
    return true;
}

void AudioCapture::stop()
{
    if (m_source) {
        m_source->stop();
    }
    m_device = nullptr;
    m_source.reset();
    emit levelChanged(0.0f);
}

bool AudioCapture::isActive() const
{
    return m_source && m_source->state() == QAudio::ActiveState;
}

void AudioCapture::onReadyRead()
{
    if (!m_device) {
        return;
    }
    const QByteArray data = m_device->readAll();
    if (data.isEmpty()) {
        return;
    }

    double sum = 0.0;
    int samples = 0;
    const auto *pcm = reinterpret_cast<const qint16 *>(data.constData());
    for (qsizetype i = 0; i + qsizetype(sizeof(qint16)) <= data.size(); i += sizeof(qint16)) {
        const double sample = pcm[samples] / 32768.0;
        sum += sample * sample;
        ++samples;
    }
    const float rms = samples > 0 ? static_cast<float>(std::sqrt(sum / samples)) : 0.0f;
    emit levelChanged(std::min(1.0f, rms * 8.0f));
    emit audioChunk(data);
}

} // namespace speecher
