#include "platform/win/WinCommunicationsAudioInput.h"

#include "platform/audio/AudioPcmConverter.h"
#include "platform/audio/QtAudioInput.h"
#include "recording/RecordingPresentation.h"

#include <QDebug>
#include <QScopeGuard>

#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <string>
#include <utility>

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
    MicrophoneCapture(const QString &deviceId, EchoControlFactory echoControl)
        : m_deviceId(deviceId)
        , m_echoControl(std::move(echoControl))
    {
    }
    ~MicrophoneCapture() override
    {
        if (m_client) {
            m_client->Stop();
        }
    }
    Q_DISABLE_COPY_MOVE(MicrophoneCapture)

    QString open() override;
    QString read(QList<QByteArray> *chunks) override;
    QString echoCancellationWarning() const override;

private:
    QString m_deviceId;
    EchoControlFactory m_echoControl;
    // Only where the echo canceller lets Speecher choose what it cancels
    // (Windows 11 22H2 and later); elsewhere Windows chooses.
    std::unique_ptr<EchoReferenceFollower> m_echoReference;
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
        return result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ? noMicrophoneText() : openError(result);
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
    if (const ComPtr<IAcousticEchoCancellationControl> echoControl = m_echoControl(client.Get())) {
        m_echoReference = std::make_unique<EchoReferenceFollower>(enumerator.Get(), echoControl.Get());
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

QString MicrophoneCapture::read(QList<QByteArray> *chunks)
{
    if (m_echoReference) {
        m_echoReference->update();
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
        // A packet marked silent has data to be ignored.
        pcm += m_converter
                   .convert(flags & AUDCLNT_BUFFERFLAGS_SILENT
                                ? silentPcm(m_format, size)
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

QString MicrophoneCapture::echoCancellationWarning() const
{
    if (!m_echoCancellationWarning.isEmpty() || !m_echoReference) {
        return m_echoCancellationWarning;
    }
    return m_echoReference->warning();
}

} // namespace

// Watches before it sets the reference, so a change in between sets it again.
// Without the watcher the reference stays where it was first set.
EchoReferenceFollower::EchoReferenceFollower(IMMDeviceEnumerator *enumerator,
                                             IAcousticEchoCancellationControl *control)
    : m_enumerator(enumerator)
    , m_control(control)
{
    auto watcher = Microsoft::WRL::Make<DefaultOutputWatcher>();
    if (watcher && watcher->changed() && SUCCEEDED(m_enumerator->RegisterEndpointNotificationCallback(watcher.Get()))) {
        m_watcher = watcher;
    }
    setReference();
}

EchoReferenceFollower::~EchoReferenceFollower()
{
    if (m_watcher) {
        m_enumerator->UnregisterEndpointNotificationCallback(m_watcher.Get());
    }
}

void EchoReferenceFollower::update()
{
    const bool changed = m_watcher && m_watcher->takeChange();
    if (changed || FAILED(m_result)) {
        setReference();
    }
}

QString EchoReferenceFollower::warning() const
{
    return FAILED(m_result) ? echoCancellationNoReferenceText(quint32(m_result)) : QString();
}

void EchoReferenceFollower::setReference()
{
    ComPtr<IMMDevice> output;
    LPWSTR id = nullptr;
    HRESULT result;
    if (SUCCEEDED(result = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &output))
        && SUCCEEDED(result = output->GetId(&id))) {
        result = m_control->SetEchoCancellationRenderEndpoint(id);
    }
    // A failure is logged once, not on each read that tries again.
    if (SUCCEEDED(result) || result != m_result) {
        qInfo().noquote() << QStringLiteral("echo cancellation reference output=\"%1\" result=0x%2")
                                 .arg(id ? QString::fromWCharArray(id) : QString())
                                 .arg(quint32(result), 8, 16, QLatin1Char('0'));
    }
    m_result = result;
    CoTaskMemFree(id);
}

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

ComPtr<IAcousticEchoCancellationControl> streamEchoControl(IAudioClient *client)
{
    ComPtr<IAcousticEchoCancellationControl> control;
    if (FAILED(client->GetService(IID_PPV_ARGS(&control)))) {
        return nullptr;
    }
    return control;
}

WinCommunicationsAudioInput::WinCommunicationsAudioInput(const AudioCaptureSettings &settings,
                                                         EchoControlFactory echoControl,
                                                         QObject *parent)
    : WinCaptureInput(
          QStringLiteral("Microphone"),
          [deviceId = settings.deviceId, echoControl = std::move(echoControl)] {
              return std::make_unique<MicrophoneCapture>(deviceId, echoControl);
          },
          settings.postRollMs,
          parent)
{
}

} // namespace speecher
