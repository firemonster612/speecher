#include "core/AudioCapture.h"

#include "core/SettingsStore.h"

#include <QAudioDevice>
#include <QDebug>
#include <QEventLoop>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace speecher {
namespace {

constexpr int kOutputSampleRate = 16000;
constexpr int kOutputBytesPerSample = int(sizeof(qint16));
constexpr int kOutputBytesPerMs = kOutputSampleRate * kOutputBytesPerSample / 1000;

QString encodedDeviceId(const QAudioDevice &device)
{
    return QString::fromLatin1(device.id().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString deviceLabel(const QAudioDevice &device)
{
    const QString description = device.description().trimmed();
    return description.isEmpty() ? QStringLiteral("Unnamed microphone") : description;
}

int bytesForMs(int milliseconds)
{
    return std::max(0, milliseconds) * kOutputBytesPerMs;
}

void appendLimited(QByteArray &buffer, const QByteArray &chunk, int maxBytes)
{
    if (maxBytes <= 0) {
        buffer.clear();
        return;
    }
    if (chunk.size() >= maxBytes) {
        buffer = chunk.right(maxBytes);
        return;
    }
    buffer.append(chunk);
    if (buffer.size() > maxBytes) {
        buffer.remove(0, buffer.size() - maxBytes);
    }
}

QAudioFormat outputFormat()
{
    QAudioFormat format;
    format.setSampleRate(kOutputSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

bool usableFormat(const QAudioFormat &format)
{
    return format.sampleRate() > 0
        && format.channelCount() > 0
        && format.bytesPerFrame() > 0
        && format.sampleFormat() != QAudioFormat::Unknown;
}

QAudioFormat sourceFormatForDevice(const QAudioDevice &device)
{
    const QAudioFormat target = outputFormat();
    if (device.isFormatSupported(target)) {
        return target;
    }

    const QAudioFormat preferred = device.preferredFormat();
    QList<int> sampleRates{preferred.sampleRate(), 48000, 44100, 32000, kOutputSampleRate};
    QList<int> channelCounts{preferred.channelCount(), 1, 2};
    const QList<QAudioFormat::SampleFormat> sampleFormats{
        QAudioFormat::Int16,
        QAudioFormat::Float,
        QAudioFormat::Int32,
        QAudioFormat::UInt8,
        preferred.sampleFormat(),
    };

    for (int sampleRate : sampleRates) {
        if (sampleRate <= 0) {
            continue;
        }
        for (int channelCount : channelCounts) {
            if (channelCount <= 0) {
                continue;
            }
            for (QAudioFormat::SampleFormat sampleFormat : sampleFormats) {
                if (sampleFormat == QAudioFormat::Unknown) {
                    continue;
                }
                QAudioFormat candidate;
                candidate.setSampleRate(sampleRate);
                candidate.setChannelCount(channelCount);
                candidate.setSampleFormat(sampleFormat);
                if (device.isFormatSupported(candidate)) {
                    return candidate;
                }
            }
        }
    }

    return preferred;
}

QString formatLabel(const QAudioFormat &format)
{
    return QStringLiteral("%1 Hz, %2 channel(s), %3")
        .arg(format.sampleRate())
        .arg(format.channelCount())
        .arg([&format] {
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8:
                return QStringLiteral("UInt8");
            case QAudioFormat::Int16:
                return QStringLiteral("Int16");
            case QAudioFormat::Int32:
                return QStringLiteral("Int32");
            case QAudioFormat::Float:
                return QStringLiteral("Float");
            case QAudioFormat::Unknown:
                return QStringLiteral("Unknown");
            }
            return QStringLiteral("Unknown");
        }());
}

QAudioDevice selectedDevice(const AudioCaptureSettings &settings, QString *error)
{
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    if (inputs.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No microphone was found. Connect or enable an input device, then try again.");
        }
        return {};
    }

    if (settings.deviceId.isEmpty()) {
        const QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (device.isNull()) {
            if (error) {
                *error = QStringLiteral("No default microphone is available. Choose a microphone in Settings.");
            }
            return {};
        }
        return device;
    }

    for (const QAudioDevice &device : inputs) {
        if (encodedDeviceId(device) == settings.deviceId) {
            return device;
        }
    }

    if (error) {
        *error = QStringLiteral("The selected microphone is not available. Choose another input device in Settings.");
    }
    return {};
}

QString sourceErrorMessageForLabel(const QString &label, QAudio::Error error)
{
    const QString microphone = label.trimmed().isEmpty() ? QStringLiteral("selected microphone") : label.trimmed();
    switch (error) {
    case QAudio::NoError:
        return QStringLiteral("Microphone \"%1\" stopped unexpectedly.").arg(microphone);
    case QAudio::OpenError:
        return QStringLiteral("Could not open microphone \"%1\". It may be busy in another app or blocked by microphone permissions.")
            .arg(microphone);
    case QAudio::IOError:
        return QStringLiteral("Microphone \"%1\" stopped while reading audio. It may have been unplugged or denied by the audio server.")
            .arg(microphone);
    case QAudio::UnderrunError:
        return QStringLiteral("Microphone \"%1\" could not provide audio quickly enough.").arg(microphone);
    case QAudio::FatalError:
        return QStringLiteral("Microphone \"%1\" failed with a fatal audio error.").arg(microphone);
    }
    return QStringLiteral("Microphone \"%1\" failed.").arg(microphone);
}

QString sourceErrorMessage(const QAudioDevice &device, QAudio::Error error)
{
    return sourceErrorMessageForLabel(deviceLabel(device), error);
}

qint16 int16FromFloat(float sample)
{
    if (sample <= -1.0f) {
        return std::numeric_limits<qint16>::min();
    }
    if (sample >= 1.0f) {
        return std::numeric_limits<qint16>::max();
    }
    return static_cast<qint16>(std::lrint(sample * std::numeric_limits<qint16>::max()));
}

void appendInt16(QByteArray &buffer, float sample)
{
    const qint16 value = int16FromFloat(std::clamp(sample, -1.0f, 1.0f));
    buffer.append(reinterpret_cast<const char *>(&value), int(sizeof(value)));
}

float sampleAt(const char *data, QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8: {
        const auto value = *reinterpret_cast<const quint8 *>(data);
        return (float(value) - 128.0f) / 128.0f;
    }
    case QAudioFormat::Int16: {
        qint16 value = 0;
        std::memcpy(&value, data, sizeof(value));
        return float(value) / 32768.0f;
    }
    case QAudioFormat::Int32: {
        qint32 value = 0;
        std::memcpy(&value, data, sizeof(value));
        return float(double(value) / 2147483648.0);
    }
    case QAudioFormat::Float: {
        float value = 0.0f;
        std::memcpy(&value, data, sizeof(value));
        return std::clamp(value, -1.0f, 1.0f);
    }
    case QAudioFormat::Unknown:
        return 0.0f;
    }
    return 0.0f;
}

QVector<float> decodeMonoSamples(const QByteArray &data, const QAudioFormat &format, QString *error)
{
    if (!usableFormat(format)) {
        if (error) {
            *error = QStringLiteral("The microphone reported an unsupported audio format.");
        }
        return {};
    }

    const int bytesPerFrame = format.bytesPerFrame();
    const int bytesPerSample = format.bytesPerSample();
    const int channels = format.channelCount();
    const int frameCount = data.size() / bytesPerFrame;
    QVector<float> samples;
    samples.reserve(frameCount);

    for (int frame = 0; frame < frameCount; ++frame) {
        const char *frameData = data.constData() + frame * bytesPerFrame;
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            sum += sampleAt(frameData + channel * bytesPerSample, format.sampleFormat());
        }
        samples.append(float(sum / channels));
    }

    return samples;
}

float rmsForPcm16(const QByteArray &pcm)
{
    const int samples = pcm.size() / int(sizeof(qint16));
    if (samples <= 0) {
        return 0.0f;
    }

    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        qint16 value = 0;
        std::memcpy(&value, pcm.constData() + i * int(sizeof(qint16)), sizeof(value));
        const double normalized = double(value) / 32768.0;
        sum += normalized * normalized;
    }
    return float(std::sqrt(sum / samples));
}

} // namespace

AudioCapture::AudioCapture(SettingsStore *settings, QObject *parent)
    : AudioInput(parent)
    , m_settings(settings)
    , m_captureSettings(currentSettings())
{
    if (m_settings) {
        connect(m_settings, &SettingsStore::audioCaptureSettingsChanged, this, &AudioCapture::handleSettingsChanged);
    }
    QTimer::singleShot(0, this, &AudioCapture::syncWarmSource);
}

QList<AudioInputDeviceInfo> AudioCapture::availableInputDevices()
{
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioInput();
    QList<AudioInputDeviceInfo> devices;
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        devices.append({
            encodedDeviceId(device),
            deviceLabel(device),
            !defaultDevice.isNull() && device.id() == defaultDevice.id(),
        });
    }
    return devices;
}

bool AudioCapture::start(QString *error)
{
    if (m_captureActive) {
        stop();
    }

    const AudioCaptureSettings settings = currentSettings();
    const bool keepWarmPreRoll = settings.mode == QStringLiteral("warm") && sourceMatches(settings);
    const QByteArray preservedPreRoll = keepWarmPreRoll ? m_preRollBuffer : QByteArray();

    resetCaptureGate(false);
    m_captureSettings = settings;
    if (keepWarmPreRoll) {
        m_preRollBuffer = preservedPreRoll;
    }

    m_captureActive = true;
    if (!ensureSourceRunning(settings, error)) {
        m_captureActive = false;
        emit levelChanged(0.0f);
        stopSource();
        return false;
    }

    if (!settings.vadEnabled) {
        flushPreRoll();
    }

    if (!waitForFirstSample(settings, error)) {
        m_captureActive = false;
        emit levelChanged(0.0f);
        stopSource();
        return false;
    }

    return true;
}

void AudioCapture::stop()
{
    if (!m_captureActive) {
        if (currentSettings().mode != QStringLiteral("warm")) {
            stopSource();
        }
        emit levelChanged(0.0f);
        return;
    }

    const AudioCaptureSettings stopSettings = m_captureSettings;
    waitForPostRoll(stopSettings);
    if (stopSettings.vadEnabled) {
        flushPendingPostRoll();
    }

    m_captureActive = false;
    resetCaptureGate(false);
    emit levelChanged(0.0f);

    const AudioCaptureSettings nextSettings = currentSettings();
    if (nextSettings.mode == QStringLiteral("warm")) {
        m_captureSettings = nextSettings;
        QString error;
        if (!ensureSourceRunning(nextSettings, &error)) {
            qWarning().noquote() << "warm audio capture could not restart message=" + error;
        }
    } else {
        stopSource();
    }
}

bool AudioCapture::isActive() const
{
    return m_captureActive;
}

void AudioCapture::handleSettingsChanged(const AudioCaptureSettings &settings)
{
    m_captureSettings = settings;
    if (m_captureActive) {
        return;
    }
    if (settings.mode == QStringLiteral("warm")) {
        QString error;
        if (!ensureSourceRunning(settings, &error)) {
            qWarning().noquote() << "warm audio capture unavailable message=" + error;
        }
    } else {
        stopSource();
    }
}

void AudioCapture::syncWarmSource()
{
    if (m_captureActive) {
        return;
    }

    const AudioCaptureSettings settings = currentSettings();
    m_captureSettings = settings;
    if (settings.mode != QStringLiteral("warm")) {
        stopSource();
        return;
    }

    QString error;
    if (!ensureSourceRunning(settings, &error)) {
        qWarning().noquote() << "warm audio capture unavailable message=" + error;
    }
}

AudioCaptureSettings AudioCapture::currentSettings() const
{
    return m_settings ? m_settings->audioCaptureSettings() : AudioCaptureSettings{};
}

bool AudioCapture::ensureSourceRunning(const AudioCaptureSettings &settings, QString *error)
{
    if (sourceMatches(settings)) {
        return true;
    }

    stopSource();

    QString deviceError;
    const QAudioDevice device = selectedDevice(settings, &deviceError);
    if (device.isNull()) {
        if (error) {
            *error = deviceError;
        }
        return false;
    }

    const QAudioFormat format = sourceFormatForDevice(device);
    if (!usableFormat(format)) {
        if (error) {
            *error = QStringLiteral("Microphone \"%1\" does not expose a usable PCM capture format.")
                .arg(deviceLabel(device));
        }
        return false;
    }

    m_source.reset(new QAudioSource(device, format, this));
    m_sourceFormat = format;
    m_currentDeviceId = encodedDeviceId(device);
    m_currentDeviceLabel = deviceLabel(device);
    m_seenFirstSample = false;
    m_conversionFailed = false;
    m_resampleBuffer.clear();
    m_nextInputPosition = 0.0;

    const int bufferBytes = std::clamp(format.sampleRate() * format.bytesPerFrame() / 20, 4096, 32768);
    m_source->setBufferSize(bufferBytes);
    connect(m_source.data(), &QAudioSource::stateChanged, this, &AudioCapture::handleSourceStateChanged);

    m_device = m_source->start();
    if (!m_device) {
        const QString message = sourceErrorMessage(device, m_source->error());
        stopSource();
        if (error) {
            *error = message;
        }
        return false;
    }
    connect(m_device, &QIODevice::readyRead, this, &AudioCapture::onReadyRead);

    qInfo().noquote() << "audio capture source started device=\"" + deviceLabel(device)
                      + "\" format=\"" + formatLabel(format)
                      + "\" output=\"16000 Hz, 1 channel, Int16\" warm="
                      + QString::number(settings.mode == QStringLiteral("warm"));
    return true;
}

bool AudioCapture::waitForFirstSample(const AudioCaptureSettings &settings, QString *error)
{
    if (m_seenFirstSample) {
        return true;
    }
    if (!m_source) {
        if (error) {
            *error = QStringLiteral("Microphone capture did not start.");
        }
        return false;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &AudioCapture::firstSampleObserved, &loop, &QEventLoop::quit);
    connect(m_source.data(), &QAudioSource::stateChanged, &loop, [&loop](QAudio::State state) {
        if (state == QAudio::StoppedState) {
            loop.quit();
        }
    });
    timeout.start(settings.readinessTimeoutMs);
    loop.exec(QEventLoop::AllEvents);

    if (m_seenFirstSample) {
        return true;
    }

    if (m_source && m_source->error() != QAudio::NoError) {
        if (error) {
            *error = sourceErrorMessageForLabel(m_currentDeviceLabel, m_source->error());
        }
        return false;
    }

    if (error) {
        *error = QStringLiteral("Microphone opened but produced no audio within %1 ms. Check microphone permissions and your audio server input settings.")
            .arg(settings.readinessTimeoutMs);
    }
    return false;
}

void AudioCapture::waitForPostRoll(const AudioCaptureSettings &settings)
{
    if (settings.postRollMs <= 0 || !m_source || !m_device) {
        return;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(settings.postRollMs);
    loop.exec(QEventLoop::AllEvents);
}

void AudioCapture::stopSource()
{
    if (!m_source && !m_device) {
        return;
    }

    m_sourceStopping = true;
    if (m_source) {
        m_source->stop();
    }
    m_device = nullptr;
    m_source.reset();
    m_sourceStopping = false;
    m_seenFirstSample = false;
    m_conversionFailed = false;
    m_currentDeviceId.clear();
    m_currentDeviceLabel.clear();
    m_sourceFormat = {};
    m_resampleBuffer.clear();
    m_nextInputPosition = 0.0;
}

void AudioCapture::resetCaptureGate(bool keepPreRoll)
{
    if (!keepPreRoll) {
        m_preRollBuffer.clear();
    }
    m_pendingPostRoll.clear();
    m_vadSpeaking = false;
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

    if (!m_seenFirstSample) {
        m_seenFirstSample = true;
        emit firstSampleObserved();
    }

    QString conversionError;
    const QByteArray pcm = toOutputPcm(data, &conversionError);
    if (!conversionError.isEmpty()) {
        if (!m_conversionFailed) {
            m_conversionFailed = true;
            failCapture(conversionError);
        }
        return;
    }
    if (pcm.isEmpty()) {
        return;
    }

    const float rms = rmsForPcm16(pcm);
    if (!m_captureActive) {
        appendPreRoll(pcm);
        return;
    }

    emit levelChanged(std::min(1.0f, rms * 8.0f));
    processOutputChunk(pcm, rms);
}

void AudioCapture::handleSourceStateChanged(QAudio::State state)
{
    if (m_sourceStopping || state != QAudio::StoppedState || !m_source) {
        return;
    }

    const QAudio::Error audioError = m_source->error();
    if (audioError == QAudio::NoError || audioError == QAudio::UnderrunError) {
        return;
    }

    const QString message = sourceErrorMessageForLabel(m_currentDeviceLabel, audioError);
    stopSource();
    failCapture(message);
}

void AudioCapture::failCapture(const QString &message)
{
    const bool wasActive = m_captureActive;
    m_captureActive = false;
    resetCaptureGate(false);
    stopSource();
    emit levelChanged(0.0f);
    if (wasActive) {
        emit failed(message);
    }
}

QByteArray AudioCapture::toOutputPcm(const QByteArray &data, QString *error)
{
    if (m_sourceFormat.sampleRate() == kOutputSampleRate
        && m_sourceFormat.channelCount() == 1
        && m_sourceFormat.sampleFormat() == QAudioFormat::Int16) {
        return data.left(data.size() - data.size() % int(sizeof(qint16)));
    }

    const QVector<float> samples = decodeMonoSamples(data, m_sourceFormat, error);
    if (samples.isEmpty()) {
        return {};
    }
    return encodeOutputSamples(samples);
}

QByteArray AudioCapture::encodeOutputSamples(const QVector<float> &samples)
{
    if (samples.isEmpty()) {
        return {};
    }

    if (m_sourceFormat.sampleRate() == kOutputSampleRate) {
        QByteArray pcm;
        pcm.reserve(samples.size() * int(sizeof(qint16)));
        for (float sample : samples) {
            appendInt16(pcm, sample);
        }
        return pcm;
    }

    m_resampleBuffer += samples;
    if (m_resampleBuffer.size() < 2) {
        return {};
    }

    const double inputStep = double(m_sourceFormat.sampleRate()) / double(kOutputSampleRate);
    QByteArray pcm;
    while (m_nextInputPosition + 1.0 < double(m_resampleBuffer.size())) {
        const int index = int(std::floor(m_nextInputPosition));
        const double fraction = m_nextInputPosition - double(index);
        const float sample = float(double(m_resampleBuffer.at(index)) * (1.0 - fraction)
                                   + double(m_resampleBuffer.at(index + 1)) * fraction);
        appendInt16(pcm, sample);
        m_nextInputPosition += inputStep;
    }

    const int removable = std::min(int(std::floor(m_nextInputPosition)), int(m_resampleBuffer.size()) - 1);
    if (removable > 0) {
        m_resampleBuffer.remove(0, removable);
        m_nextInputPosition -= removable;
    }

    return pcm;
}

void AudioCapture::processOutputChunk(const QByteArray &pcm, float rms)
{
    if (!m_captureSettings.vadEnabled) {
        emit audioChunk(pcm);
        return;
    }

    appendPreRoll(pcm);
    const bool voiced = rms >= float(m_captureSettings.vadThresholdPercent) / 100.0f;
    if (!m_vadSpeaking) {
        if (!voiced) {
            return;
        }

        m_vadSpeaking = true;
        if (m_preRollBuffer.isEmpty()) {
            emit audioChunk(pcm);
        } else {
            emit audioChunk(m_preRollBuffer);
        }
        m_preRollBuffer.clear();
        m_pendingPostRoll.clear();
        return;
    }

    if (voiced) {
        if (!m_pendingPostRoll.isEmpty()) {
            emit audioChunk(m_pendingPostRoll);
            m_pendingPostRoll.clear();
        }
        emit audioChunk(pcm);
        return;
    }

    const int postRollBytes = bytesForMs(m_captureSettings.postRollMs);
    if (postRollBytes <= 0) {
        m_pendingPostRoll.clear();
        m_preRollBuffer.clear();
        m_vadSpeaking = false;
        return;
    }

    m_pendingPostRoll.append(pcm);
    if (m_pendingPostRoll.size() >= postRollBytes) {
        emit audioChunk(m_pendingPostRoll.left(postRollBytes));
        const int keepBytes = std::min(int(m_pendingPostRoll.size()), bytesForMs(m_captureSettings.preRollMs));
        m_preRollBuffer = m_pendingPostRoll.right(keepBytes);
        m_pendingPostRoll.clear();
        m_vadSpeaking = false;
    }
}

void AudioCapture::appendPreRoll(const QByteArray &pcm)
{
    appendLimited(m_preRollBuffer, pcm, bytesForMs(m_captureSettings.preRollMs));
}

void AudioCapture::flushPreRoll()
{
    if (!m_captureActive || m_preRollBuffer.isEmpty()) {
        m_preRollBuffer.clear();
        return;
    }

    emit audioChunk(m_preRollBuffer);
    m_preRollBuffer.clear();
}

void AudioCapture::flushPendingPostRoll()
{
    if (!m_captureActive || m_pendingPostRoll.isEmpty()) {
        m_pendingPostRoll.clear();
        return;
    }

    const int postRollBytes = bytesForMs(m_captureSettings.postRollMs);
    emit audioChunk(postRollBytes > 0 ? m_pendingPostRoll.left(postRollBytes) : m_pendingPostRoll);
    m_pendingPostRoll.clear();
}

bool AudioCapture::sourceMatches(const AudioCaptureSettings &settings) const
{
    if (!m_source || !m_device) {
        return false;
    }

    if (settings.deviceId.isEmpty()) {
        const QAudioDevice defaultDevice = QMediaDevices::defaultAudioInput();
        return !defaultDevice.isNull() && m_currentDeviceId == encodedDeviceId(defaultDevice);
    }

    return m_currentDeviceId == settings.deviceId;
}

} // namespace speecher
