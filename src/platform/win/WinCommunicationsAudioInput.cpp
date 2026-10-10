#include "platform/win/WinCommunicationsAudioInput.h"

#include "platform/audio/AudioPcmConverter.h"
#include "platform/win/WinSystemAudioInput.h"
#include "recording/RecordingPresentation.h"

#include <QDebug>
#include <QScopeGuard>

#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <string>

namespace speecher {
namespace {

using Microsoft::WRL::ComPtr;

QString openError(HRESULT result)
{
    return QStringLiteral("Could not open the microphone: %1").arg(audioErrorText(result, AudioEndpoint::Microphone));
}

// A microphone that is gone falls back to the default one, Qt's default, as
// dictation's does.
HRESULT findMicrophone(IMMDeviceEnumerator *enumerator, const QString &deviceId, IMMDevice **device)
{
    if (!deviceId.isEmpty()) {
        const std::wstring id = microphoneEndpointId(deviceId).toStdWString();
        ComPtr<IMMDevice> named;
        DWORD state = 0;
        if (SUCCEEDED(enumerator->GetDevice(id.c_str(), &named)) && SUCCEEDED(named->GetState(&state))
            && state == DEVICE_STATE_ACTIVE) {
            *device = named.Detach();
            return S_OK;
        }
    }
    return enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, device);
}

// The category that brings in the driver's echo cancellation; it has to be
// set before the stream is initialized.
HRESULT useCommunicationsCategory(IAudioClient *client)
{
    ComPtr<IAudioClient2> client2;
    const HRESULT result = client->QueryInterface(IID_PPV_ARGS(&client2));
    if (FAILED(result)) {
        return result;
    }
    AudioClientProperties properties{};
    properties.cbSize = sizeof(properties);
    properties.eCategory = AudioCategory_Communications;
    return client2->SetClientProperties(&properties);
}

// Windows can turn other audio down while a call's stream is open, as Sound
// settings > Communications says, which would make the call quieter. A
// communications microphone did not do it on Windows 11 24H2, but Windows
// documents capture streams as doing it, so the stream opts out where it can.
void keepOtherAudioLevel(IAudioClient *client)
{
    ComPtr<IAudioClientDuckingControl> ducking;
    if (SUCCEEDED(client->GetService(IID_PPV_ARGS(&ducking)))) {
        ducking->SetDuckingOptionsForCurrentStream(AUDIO_DUCKING_OPTIONS_DO_NOT_DUCK_OTHER_STREAMS);
    }
}

// Cancels what the default output plays, the output system audio captures,
// where the echo canceller lets Speecher choose (Windows 11 22H2 and later);
// elsewhere Windows chooses.
void cancelEchoOfDefaultOutput(IMMDeviceEnumerator *enumerator, IAcousticEchoCancellationControl *control)
{
    ComPtr<IMMDevice> output;
    LPWSTR id = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &output)) || FAILED(output->GetId(&id))) {
        return;
    }
    const HRESULT result = control->SetEchoCancellationRenderEndpoint(id);
    qInfo().noquote() << QStringLiteral("recording microphone cancels the echo of output=\"%1\" result=0x%2")
                             .arg(QString::fromWCharArray(id))
                             .arg(quint32(result), 8, 16, QLatin1Char('0'));
    CoTaskMemFree(id);
}

HRESULT streamEffects(IAudioClient *client, QList<AUDIO_EFFECT> *effects)
{
    ComPtr<IAudioEffectsManager> manager;
    AUDIO_EFFECT *list = nullptr;
    UINT32 count = 0;
    HRESULT result;
    if (FAILED(result = client->GetService(IID_PPV_ARGS(&manager)))
        || FAILED(result = manager->GetAudioEffects(&list, &count))) {
        return result;
    }
    *effects = QList<AUDIO_EFFECT>(list, list + count);
    CoTaskMemFree(list);
    return S_OK;
}

class MicrophoneCapture final : public WinCaptureStream {
public:
    explicit MicrophoneCapture(const QString &deviceId)
        : m_deviceId(deviceId)
    {
    }
    ~MicrophoneCapture() override
    {
        if (m_outputWatcher) {
            m_enumerator->UnregisterEndpointNotificationCallback(m_outputWatcher.Get());
        }
        if (m_client) {
            m_client->Stop();
        }
    }
    Q_DISABLE_COPY_MOVE(MicrophoneCapture)

    QString open() override;
    QString read(QList<QByteArray> *chunks) override;
    QString echoCancellationWarning() const override { return m_echoCancellationWarning; }

private:
    // Moves the echo canceller's reference to each new default output, as
    // system audio moves to it.
    void followDefaultOutput();

    QString m_deviceId;
    ComPtr<IMMDeviceEnumerator> m_enumerator;
    ComPtr<IAcousticEchoCancellationControl> m_echoControl;
    ComPtr<DefaultOutputWatcher> m_outputWatcher;
    ComPtr<IAudioClient> m_client;
    ComPtr<IAudioCaptureClient> m_capture;
    int m_bytesPerFrame = 0;
    QAudioFormat m_format;
    AudioPcmConverter m_converter;
    QString m_echoCancellationWarning;
};

QString MicrophoneCapture::open()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    HRESULT result;
    if (FAILED(result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                         nullptr,
                                         CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(&enumerator)))
        || FAILED(result = findMicrophone(enumerator.Get(), m_deviceId, &device))
        || FAILED(result = device->Activate(__uuidof(IAudioClient),
                                            CLSCTX_INPROC_SERVER,
                                            nullptr,
                                            reinterpret_cast<void **>(client.GetAddressOf())))) {
        return result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
            ? QStringLiteral("No microphone was found. Connect or enable an input device, then try again.")
            : openError(result);
    }
    const HRESULT category = useCommunicationsCategory(client.Get());
    WAVEFORMATEX *mixFormat = nullptr;
    result = client->GetMixFormat(&mixFormat);
    if (FAILED(result)) {
        return openError(result);
    }
    const auto freeMixFormat = qScopeGuard([mixFormat] { CoTaskMemFree(mixFormat); });
    const QAudioFormat format = audioFormatForWave(*mixFormat);
    if (!format.isValid()) {
        return openError(AUDCLNT_E_UNSUPPORTED_FORMAT);
    }
    result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kCaptureBufferDuration, 0, mixFormat, nullptr);
    if (FAILED(result)) {
        return openError(result);
    }
    keepOtherAudioLevel(client.Get());
    m_enumerator = enumerator;
    if (SUCCEEDED(client->GetService(IID_PPV_ARGS(&m_echoControl)))) {
        followDefaultOutput();
    }
    QList<AUDIO_EFFECT> effects;
    const HRESULT effectsQuery = streamEffects(client.Get(), &effects);
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(result = client->GetService(IID_PPV_ARGS(&capture))) || FAILED(result = client->Start())) {
        return openError(result);
    }
    m_client = client;
    m_capture = capture;
    m_bytesPerFrame = mixFormat->nBlockAlign;
    m_format = format;
    m_converter.reset(format);
    m_echoCancellationWarning = communicationsEchoCancellationWarning(category, effectsQuery, effects);
    return {};
}

// Watches before it sets the reference, so a change in between sets it again.
void MicrophoneCapture::followDefaultOutput()
{
    auto watcher = Microsoft::WRL::Make<DefaultOutputWatcher>();
    if (watcher && watcher->changed()
        && SUCCEEDED(m_enumerator->RegisterEndpointNotificationCallback(watcher.Get()))) {
        m_outputWatcher = watcher;
    }
    cancelEchoOfDefaultOutput(m_enumerator.Get(), m_echoControl.Get());
}

QString MicrophoneCapture::read(QList<QByteArray> *chunks)
{
    if (m_outputWatcher && m_outputWatcher->takeChange()) {
        cancelEchoOfDefaultOutput(m_enumerator.Get(), m_echoControl.Get());
    }
    QByteArray pcm;
    UINT32 packetFrames = 0;
    HRESULT result;
    while (SUCCEEDED(result = m_capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(result = m_capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
            break;
        }
        const qsizetype size = qsizetype(frames) * m_bytesPerFrame;
        // A packet marked silent has data to be ignored, and unsigned samples
        // are silent at the middle of their range.
        pcm += m_converter
                   .convert(flags & AUDCLNT_BUFFERFLAGS_SILENT
                                ? QByteArray(size, m_format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0')
                                : QByteArray(reinterpret_cast<const char *>(data), size))
                   .pcm16Mono16k;
        if (FAILED(result = m_capture->ReleaseBuffer(frames))) {
            break;
        }
    }
    if (!pcm.isEmpty()) {
        chunks->append(pcm);
    }
    if (FAILED(result)) {
        return QStringLiteral("Microphone capture stopped: %1").arg(audioErrorText(result, AudioEndpoint::Microphone));
    }
    return {};
}

} // namespace

QString microphoneEndpointId(const QString &deviceId)
{
    return QString::fromUtf8(
        QByteArray::fromBase64(deviceId.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString communicationsEchoCancellationWarning(HRESULT category,
                                              HRESULT effectsQuery,
                                              const QList<AUDIO_EFFECT> &effects)
{
    if (FAILED(category)) {
        return echoCancellationNoCallStreamText(quint32(category));
    }
    if (FAILED(effectsQuery)) {
        return echoCancellationUnknownText();
    }
    for (const AUDIO_EFFECT &effect : effects) {
        if (effect.id == AUDIO_EFFECT_TYPE_ACOUSTIC_ECHO_CANCELLATION) {
            return effect.state == AUDIO_EFFECT_STATE_ON ? QString() : echoCancellationTurnedOffText();
        }
    }
    return echoCancellationNotOfferedText();
}

WinCommunicationsAudioInput::WinCommunicationsAudioInput(const AudioCaptureSettings &settings, QObject *parent)
    : WinCaptureInput(
          QStringLiteral("Microphone"),
          [deviceId = settings.deviceId] { return std::make_unique<MicrophoneCapture>(deviceId); },
          settings.postRollMs,
          parent)
{
}

} // namespace speecher
