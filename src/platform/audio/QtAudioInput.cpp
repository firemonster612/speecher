#include "platform/audio/QtAudioInput.h"

#include <QAudioDevice>
#include <QDebug>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <utility>

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
            default:
                return QStringLiteral("Unknown");
            }
        }());
}

QAudioDevice selectedDevice(const AudioCaptureSettings &settings, QString *error)
{
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    if (inputs.isEmpty()) {
        if (error) {
            *error = noMicrophoneText();
        }
        return {};
    }

    if (settings.deviceId.isEmpty()) {
        const QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (device.isNull()) {
            return inputs.first();
        }
        return device;
    }

    for (const QAudioDevice &device : inputs) {
        if (encodedDeviceId(device) == settings.deviceId) {
            return device;
        }
    }

    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioInput();
    if (!defaultDevice.isNull()) {
        return defaultDevice;
    }
    return inputs.first();
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
#if QT_VERSION < QT_VERSION_CHECK(6, 11, 0)
    case QAudio::UnderrunError:
        return QStringLiteral("Microphone \"%1\" could not provide audio quickly enough.").arg(microphone);
#endif
    case QAudio::FatalError:
        return QStringLiteral("Microphone \"%1\" failed with a fatal audio error.").arg(microphone);
    default:
        return QStringLiteral("Microphone \"%1\" failed.").arg(microphone);
    }
}

bool isIgnorableStoppedSourceError(QAudio::Error error)
{
    if (error == QAudio::NoError) {
        return true;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 11, 0)
    return error == QAudio::UnderrunError;
#else
    return false;
#endif
}

QString sourceErrorMessage(const QAudioDevice &device, QAudio::Error error)
{
    return sourceErrorMessageForLabel(deviceLabel(device), error);
}

} // namespace

QString noMicrophoneText()
{
    return QStringLiteral("No microphone was found. Connect or enable an input device, then try again.");
}

QtAudioInput::QtAudioInput(const AudioCaptureSettings &settings, QObject *parent)
    : AudioInput(parent)
    , m_mediaDevices(this)
    , m_captureSettings(settings)
{
    m_gate.setSizes(bytesForMs(settings.preRollMs), bytesForMs(settings.postRollMs));
    m_readinessTimer.setSingleShot(true);
    m_postRollTimer.setSingleShot(true);
    connect(&m_readinessTimer, &QTimer::timeout, this, &QtAudioInput::readinessTimedOut);
    connect(&m_postRollTimer, &QTimer::timeout, this, &QtAudioInput::finishStop);
}

QList<AudioInputDeviceInfo> QtAudioInput::availableInputDevices()
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

void QtAudioInput::warmUp()
{
    syncWarmSource();
}

void QtAudioInput::useDevice(const QString &deviceId)
{
    m_captureSettings.deviceId = deviceId;
}

void QtAudioInput::start()
{
    if (m_capture != Capture::Off) {
        endCapture();
    }
    dropReports();

    const AudioCaptureSettings settings = currentSettings();
    m_gate.reset(settings.mode == QStringLiteral("warm") && sourceMatches(settings));
    m_captureSettings = settings;

    m_capture = Capture::Starting;
    QString error;
    if (!ensureSourceRunning(settings, &error)) {
        m_capture = Capture::Off;
        stopSource();
        reportLater([this, error] {
            emit levelChanged(0.0f);
            emit failed(error);
        });
        return;
    }
    // A warm source already flowing is ready now.
    if (m_seenFirstSample) {
        reportLater([this] { captureStarted(); });
        return;
    }
    m_readinessTimer.start(settings.readinessTimeoutMs);
}

// The first audio since the start: the pre-roll goes out ahead of it.
void QtAudioInput::captureStarted()
{
    if (m_capture != Capture::Starting) {
        return;
    }
    m_readinessTimer.stop();
    m_capture = Capture::On;
    emit started();
    if (!m_captureSettings.vadEnabled) {
        flushPreRoll();
    }
}

void QtAudioInput::readinessTimedOut()
{
    if (m_capture != Capture::Starting) {
        return;
    }
    if (m_source && m_source->error() != QAudio::NoError) {
        failCapture(sourceErrorMessageForLabel(m_currentDeviceLabel, m_source->error()));
        return;
    }
    failCapture(QStringLiteral("Microphone opened but produced no audio within %1 ms. Check microphone permissions and your audio server input settings.")
                    .arg(m_captureSettings.readinessTimeoutMs));
}

void QtAudioInput::stop()
{
    switch (m_capture) {
    case Capture::Off:
        if (currentSettings().mode != QStringLiteral("warm")) {
            stopSource();
        }
        return;
    case Capture::Stopping:
        return;
    case Capture::Starting:
        endCapture();
        reportLater([this] {
            emit levelChanged(0.0f);
            emit stopped();
        });
        return;
    case Capture::On:
        break;
    }
    m_capture = Capture::Stopping;
    if (m_captureSettings.postRollMs > 0 && m_source && m_device) {
        m_postRollTimer.start(m_captureSettings.postRollMs);
    } else {
        reportLater([this] { finishStop(); });
    }
}

void QtAudioInput::finishStop()
{
    if (m_capture != Capture::Stopping) {
        return;
    }
    if (m_captureSettings.vadEnabled) {
        flushPendingPostRoll();
        // A handler of the post-roll may have aborted or started again.
        if (m_capture != Capture::Stopping) {
            return;
        }
    }
    endCapture();
    emit levelChanged(0.0f);
    emit stopped();
}

void QtAudioInput::abort()
{
    dropReports();
    if (m_capture == Capture::Off) {
        if (currentSettings().mode != QStringLiteral("warm")) {
            stopSource();
        }
        return;
    }
    endCapture();
    emit levelChanged(0.0f);
}

void QtAudioInput::endCapture()
{
    m_capture = Capture::Off;
    m_readinessTimer.stop();
    m_postRollTimer.stop();
    m_gate.reset(false);
    const AudioCaptureSettings settings = currentSettings();
    if (settings.mode == QStringLiteral("warm")) {
        QString error;
        if (!ensureSourceRunning(settings, &error)) {
            qWarning().noquote() << "warm audio capture could not restart message=" + error;
        }
    } else {
        stopSource();
    }
}

void QtAudioInput::clearPreRoll()
{
    m_gate.takePreRoll();
}

bool QtAudioInput::isActive() const
{
    return m_capture != Capture::Off;
}

void QtAudioInput::keepSilence()
{
    m_keepsSilence = true;
    m_captureSettings.vadEnabled = false;
}

void QtAudioInput::applySettings(const AudioCaptureSettings &settings)
{
    m_captureSettings = settings;
    m_captureSettings.vadEnabled = settings.vadEnabled && !m_keepsSilence;
    m_gate.setSizes(bytesForMs(settings.preRollMs), bytesForMs(settings.postRollMs));
    if (m_capture != Capture::Off) {
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

void QtAudioInput::ensureMediaDevicesInitialized()
{
    if (m_mediaDevicesInitialized) {
        return;
    }
    m_mediaDevicesInitialized = true;
    connect(&m_mediaDevices,
            &QMediaDevices::audioInputsChanged,
            this,
            &QtAudioInput::handleAudioInputsChanged);
}

void QtAudioInput::handleAudioInputsChanged()
{
    if (!m_currentDeviceId.isEmpty()) {
        const bool currentStillAvailable = std::ranges::any_of(
            QMediaDevices::audioInputs(),
            [this](const QAudioDevice &device) {
                return encodedDeviceId(device) == m_currentDeviceId;
            });
        if (!currentStillAvailable && m_capture != Capture::Off) {
            failCapture(QStringLiteral(
                "Microphone \"%1\" was disconnected. Reconnect it or choose another input in Settings.")
                            .arg(m_currentDeviceLabel));
            return;
        }
    }
    if (m_capture == Capture::Off) {
        syncWarmSource();
    }
}

void QtAudioInput::syncWarmSource()
{
    ensureMediaDevicesInitialized();
    if (m_capture != Capture::Off) {
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

AudioCaptureSettings QtAudioInput::currentSettings() const
{
    return m_captureSettings;
}

bool QtAudioInput::ensureSourceRunning(const AudioCaptureSettings &settings, QString *error)
{
    ensureMediaDevicesInitialized();
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
    m_converter.reset(format);
    m_currentDeviceId = encodedDeviceId(device);
    m_currentDeviceLabel = deviceLabel(device);
    m_seenFirstSample = false;
    m_conversionFailed = false;

    const int bufferBytes = std::clamp(format.sampleRate() * format.bytesPerFrame() / 20, 4096, 32768);
    m_source->setBufferSize(bufferBytes);
    connect(m_source.data(), &QAudioSource::stateChanged, this, &QtAudioInput::handleSourceStateChanged);

    m_device = m_source->start();
    if (!m_device) {
        const QString message = sourceErrorMessage(device, m_source->error());
        stopSource();
        if (error) {
            *error = message;
        }
        return false;
    }
    connect(m_device, &QIODevice::readyRead, this, &QtAudioInput::onReadyRead);

    qInfo().noquote() << "audio capture source started device=\"" + deviceLabel(device)
                      + "\" format=\"" + formatLabel(format)
                      + "\" output=\"16000 Hz, 1 channel, Int16\" warm="
                      + QString::number(settings.mode == QStringLiteral("warm"));
    return true;
}

void QtAudioInput::stopSource()
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
    m_converter.reset({});
}

void QtAudioInput::onReadyRead()
{
    if (!m_device) {
        return;
    }

    const QByteArray data = m_device->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_seenFirstSample = true;
    const AudioPcmConversion conversion = m_converter.convert(data);
    if (!conversion.error.isEmpty()) {
        if (!m_conversionFailed) {
            m_conversionFailed = true;
            failCapture(conversion.error);
        }
        return;
    }
    if (m_capture == Capture::Starting) {
        captureStarted();
    }
    const QByteArray &pcm = conversion.pcm16Mono16k;
    if (pcm.isEmpty()) {
        return;
    }

    if (m_capture == Capture::Off) {
        appendPreRoll(pcm);
        return;
    }

    emit levelChanged(std::min(1.0f, conversion.rms * 8.0f));
    processOutputChunk(pcm, conversion.rms);
}

void QtAudioInput::handleSourceStateChanged(QAudio::State state)
{
    if (m_sourceStopping || state != QAudio::StoppedState || !m_source) {
        return;
    }

    const QAudio::Error audioError = m_source->error();
    if (isIgnorableStoppedSourceError(audioError)) {
        return;
    }

    const QString message = sourceErrorMessageForLabel(m_currentDeviceLabel, audioError);
    stopSource();
    failCapture(message);
}

// A capture that was asked to stop reports stopped(), as it was asked.
void QtAudioInput::failCapture(const QString &message)
{
    const Capture was = std::exchange(m_capture, Capture::Off);
    m_readinessTimer.stop();
    m_postRollTimer.stop();
    m_gate.reset(false);
    stopSource();
    emit levelChanged(0.0f);
    if (was == Capture::Stopping) {
        emit stopped();
    } else if (was != Capture::Off) {
        emit failed(message);
    }
}

void QtAudioInput::processOutputChunk(const QByteArray &pcm, float rms)
{
    if (!m_captureSettings.vadEnabled) {
        emit audioChunk(pcm);
        return;
    }

    const QList<QByteArray> chunks = m_gate.pass(pcm, isVoiced(rms, m_captureSettings.vadThresholdPercent));
    for (const QByteArray &chunk : chunks) {
        emit audioChunk(chunk);
    }
}

void QtAudioInput::appendPreRoll(const QByteArray &pcm)
{
    m_gate.holdQuiet(pcm);
}

void QtAudioInput::flushPreRoll()
{
    const QByteArray preRoll = m_gate.takePreRoll();
    if (m_capture != Capture::Off && !preRoll.isEmpty()) {
        emit audioChunk(preRoll);
    }
}

void QtAudioInput::flushPendingPostRoll()
{
    const QByteArray postRoll = m_gate.flushPostRoll();
    if (m_capture != Capture::Off && !postRoll.isEmpty()) {
        emit audioChunk(postRoll);
    }
}

bool QtAudioInput::sourceMatches(const AudioCaptureSettings &settings) const
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
