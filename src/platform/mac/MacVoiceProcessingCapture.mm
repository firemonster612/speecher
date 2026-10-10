#include "platform/mac/MacMicrophoneInput.h"

#include "platform/audio/QtAudioInput.h"
#include "recording/RecordingPresentation.h"

#include <QAudioDevice>
#include <QDebug>

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace speecher {
namespace {

// An I/O unit's output element plays to the sound output; its input element
// captures from the microphone.
constexpr AudioUnitElement kOutputElement = 0;
constexpr AudioUnitElement kInputElement = 1;

constexpr AudioObjectPropertyAddress kDeviceIsAlive{
    kAudioDevicePropertyDeviceIsAlive,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

template<typename T>
OSStatus setProperty(AudioUnit unit, AudioUnitPropertyID property, AudioUnitScope scope,
                     AudioUnitElement element, const T &value)
{
    return AudioUnitSetProperty(unit, property, scope, element, &value, sizeof(value));
}

// Qt names a macOS audio device by its UID.
AudioDeviceID deviceForUid(const QByteArray &uid)
{
    CFStringRef string = CFStringCreateWithBytes(nullptr,
                                                 reinterpret_cast<const UInt8 *>(uid.constData()),
                                                 uid.size(),
                                                 kCFStringEncodingUTF8,
                                                 false);
    if (!string) {
        return kAudioObjectUnknown;
    }
    const AudioObjectPropertyAddress translate{
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &translate, sizeof(string), &string, &size, &device);
    CFRelease(string);
    return status == noErr ? device : kAudioObjectUnknown;
}

AudioDeviceID defaultOutputDevice()
{
    const AudioObjectPropertyAddress defaultOutput{
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultOutput, 0, nullptr, &size, &device) != noErr) {
        return kAudioObjectUnknown;
    }
    return device;
}

// Interleaved float, which the converter reads whatever the rate and
// channels.
AudioStreamBasicDescription floatFormat(Float64 sampleRate, UInt32 channels)
{
    AudioStreamBasicDescription format{};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = channels * sizeof(float);
    format.mBytesPerPacket = format.mBytesPerFrame;
    return format;
}

// The VoiceProcessingIO unit rather than AVAudioEngine's voice processing:
// it takes the microphone and the output it cancels as devices of their own,
// says why it could not start, and leaves the HAL output unit, which records
// without it, the same setup and callback.
class VoiceProcessingCapture final : public MacMicrophoneCapture {
public:
    explicit VoiceProcessingCapture(const AudioCaptureSettings &settings)
        : m_settings(settings)
    {
    }

    ~VoiceProcessingCapture() override
    {
        if (m_listenedDevice != kAudioObjectUnknown) {
            AudioObjectRemovePropertyListener(m_listenedDevice, &kDeviceIsAlive, onDeviceChanged, this);
        }
        closeUnit();
    }

    QString open(Deliver deliver, Fail fail) override
    {
        m_deliver = std::move(deliver);
        m_fail = std::move(fail);
        QString error;
        const QAudioDevice device = QtAudioInput::selectedInputDevice(m_settings, &error);
        if (device.isNull()) {
            return error;
        }
        m_label = device.description();
        const AudioDeviceID input = deviceForUid(device.id());
        if (input == kAudioObjectUnknown) {
            return microphoneDisconnectedText(m_label);
        }
        if (const OSStatus status = openUnit(kAudioUnitSubType_VoiceProcessingIO, input); status != noErr) {
            qWarning().noquote() << "voice processing could not start device=\"" + m_label
                                        + "\" error=" + QString::number(status);
            closeUnit();
            m_echoCancellationWarning = echoCancellationNoVoiceProcessingText(int(status));
            if (const OSStatus fallback = openUnit(kAudioUnitSubType_HALOutput, input); fallback != noErr) {
                closeUnit();
                return QStringLiteral("Could not open microphone \"%1\" (error %2). It may be busy in another app "
                                      "or blocked by microphone permissions.")
                    .arg(m_label)
                    .arg(fallback);
            }
        }
        if (AudioObjectAddPropertyListener(input, &kDeviceIsAlive, onDeviceChanged, this) == noErr) {
            m_listenedDevice = input;
        }
        qInfo().noquote() << "recording microphone started device=\"" + m_label + "\" voiceProcessing="
                                 + QString::number(m_echoCancellationWarning.isEmpty()) + " format=\""
                                 + QString::number(m_format.sampleRate()) + " Hz, "
                                 + QString::number(m_format.channelCount()) + " channel(s), Float\"";
        return {};
    }

    QAudioFormat format() const override { return m_format; }
    QString echoCancellationWarning() const override { return m_echoCancellationWarning; }

private:
    OSStatus openUnit(OSType subType, AudioDeviceID input)
    {
        const AudioComponentDescription description{kAudioUnitType_Output, subType, kAudioUnitManufacturer_Apple, 0, 0};
        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!component) {
            return kAudio_UnimplementedError;
        }
        OSStatus status = AudioComponentInstanceNew(component, &m_unit);
        if (status != noErr) {
            m_unit = nullptr;
            return status;
        }
        const bool voiceProcessing = subType == kAudioUnitSubType_VoiceProcessingIO;
        status = setProperty(m_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, kInputElement, UInt32(1));
        // Voice processing plays silence to the output whose playback it
        // cancels; the HAL unit needs no output, and takes its one device for
        // its input once the output is off.
        if (status == noErr && !voiceProcessing) {
            status = setProperty(
                m_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, kOutputElement, UInt32(0));
        }
        if (status == noErr) {
            status = setProperty(m_unit,
                                 kAudioOutputUnitProperty_CurrentDevice,
                                 kAudioUnitScope_Global,
                                 voiceProcessing ? kInputElement : kOutputElement,
                                 input);
        }
        if (const AudioDeviceID output = defaultOutputDevice(); status == noErr && voiceProcessing
                                                                 && output != kAudioObjectUnknown) {
            status = setProperty(
                m_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, kOutputElement, output);
        }

        // Voice processing can capture at another rate and channel count
        // than the microphone's own, so the format comes from the unit.
        AudioStreamBasicDescription captured{};
        UInt32 size = sizeof(captured);
        if (status == noErr) {
            status = AudioUnitGetProperty(
                m_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, kInputElement, &captured, &size);
        }
        if (status == noErr && (captured.mSampleRate <= 0 || captured.mChannelsPerFrame == 0)) {
            status = kAudioUnitErr_FormatNotSupported;
        }
        if (status == noErr) {
            status = setProperty(m_unit,
                                 kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output,
                                 kInputElement,
                                 floatFormat(captured.mSampleRate, captured.mChannelsPerFrame));
        }
        if (status == noErr && voiceProcessing) {
            status = setProperty(m_unit,
                                 kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Input,
                                 kOutputElement,
                                 floatFormat(captured.mSampleRate, 1));
        }
        if (status == noErr) {
            status = setProperty(m_unit,
                                 kAudioOutputUnitProperty_SetInputCallback,
                                 kAudioUnitScope_Global,
                                 kInputElement,
                                 AURenderCallbackStruct{onInput, this});
        }
        if (status == noErr && voiceProcessing) {
            status = setProperty(m_unit,
                                 kAudioUnitProperty_SetRenderCallback,
                                 kAudioUnitScope_Input,
                                 kOutputElement,
                                 AURenderCallbackStruct{onPlayout, this});
        }
        if (status == noErr) {
            status = AudioUnitInitialize(m_unit);
        }
        if (status == noErr && voiceProcessing) {
            setLeastDucking();
        }
        UInt32 maximumFrames = 0;
        size = sizeof(maximumFrames);
        if (status == noErr) {
            status = AudioUnitGetProperty(
                m_unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximumFrames, &size);
        }
        if (status != noErr) {
            return status;
        }
        m_format.setSampleRate(int(captured.mSampleRate));
        m_format.setChannelCount(int(captured.mChannelsPerFrame));
        m_format.setSampleFormat(QAudioFormat::Float);
        m_buffer.resize(size_t(maximumFrames) * captured.mChannelsPerFrame);
        return AudioOutputUnitStart(m_unit);
    }

    // Voice processing turns other audio down while it runs, which would
    // make the call quieter.
    void setLeastDucking()
    {
        const AUVoiceIOOtherAudioDuckingConfiguration ducking{
            .mEnableAdvancedDucking = false,
            .mDuckingLevel = kAUVoiceIOOtherAudioDuckingLevelMin,
        };
        const OSStatus status = setProperty(
            m_unit, kAUVoiceIOProperty_OtherAudioDuckingConfiguration, kAudioUnitScope_Global, kOutputElement, ducking);
        if (status != noErr) {
            qWarning().noquote() << "voice processing ducking could not be lowered error=" + QString::number(status);
        }
    }

    void closeUnit()
    {
        if (!m_unit) {
            return;
        }
        AudioOutputUnitStop(m_unit);
        AudioUnitUninitialize(m_unit);
        AudioComponentInstanceDispose(m_unit);
        m_unit = nullptr;
    }

    // Reports why capture stopped, once, from whichever thread found it.
    void failOnce(const QString &message)
    {
        if (!m_failed.exchange(true)) {
            m_fail(message);
        }
    }

    // Runs on the I/O thread.
    static OSStatus onInput(void *context,
                            AudioUnitRenderActionFlags *flags,
                            const AudioTimeStamp *time,
                            UInt32 bus,
                            UInt32 frames,
                            AudioBufferList *)
    {
        auto *capture = static_cast<VoiceProcessingCapture *>(context);
        const auto channels = UInt32(capture->m_format.channelCount());
        if (capture->m_buffer.size() < size_t(frames) * channels) {
            capture->m_buffer.resize(size_t(frames) * channels);
        }
        AudioBufferList list{1, {{channels, UInt32(frames * channels * sizeof(float)), capture->m_buffer.data()}}};
        const OSStatus status = AudioUnitRender(capture->m_unit, flags, time, bus, frames, &list);
        if (status != noErr) {
            capture->failOnce(QStringLiteral("Microphone \"%1\" stopped while reading audio (error %2).")
                                  .arg(capture->m_label)
                                  .arg(status));
            return status;
        }
        if (!capture->m_failed) {
            capture->m_deliver(QByteArrayView(static_cast<const char *>(list.mBuffers[0].mData),
                                              qsizetype(list.mBuffers[0].mDataByteSize)));
        }
        return noErr;
    }

    // Runs on the I/O thread.
    static OSStatus onPlayout(void *,
                              AudioUnitRenderActionFlags *flags,
                              const AudioTimeStamp *,
                              UInt32,
                              UInt32,
                              AudioBufferList *data)
    {
        for (UInt32 index = 0; index < data->mNumberBuffers; ++index) {
            std::memset(data->mBuffers[index].mData, 0, data->mBuffers[index].mDataByteSize);
        }
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }

    // Runs on a CoreAudio notification thread.
    static OSStatus onDeviceChanged(AudioObjectID device, UInt32, const AudioObjectPropertyAddress *, void *context)
    {
        UInt32 alive = 1;
        UInt32 size = sizeof(alive);
        if (AudioObjectGetPropertyData(device, &kDeviceIsAlive, 0, nullptr, &size, &alive) == noErr && !alive) {
            auto *capture = static_cast<VoiceProcessingCapture *>(context);
            capture->failOnce(microphoneDisconnectedText(capture->m_label));
        }
        return noErr;
    }

    AudioCaptureSettings m_settings;
    Deliver m_deliver;
    Fail m_fail;
    AudioUnit m_unit = nullptr;
    AudioDeviceID m_listenedDevice = kAudioObjectUnknown;
    QString m_label;
    QAudioFormat m_format;
    std::vector<float> m_buffer;
    std::atomic_bool m_failed = false;
    QString m_echoCancellationWarning;
};

} // namespace

std::unique_ptr<MacMicrophoneCapture> createVoiceProcessingCapture(const AudioCaptureSettings &settings)
{
    return std::make_unique<VoiceProcessingCapture>(settings);
}

} // namespace speecher
