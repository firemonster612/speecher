#include "platform/mac/MacAudioDevice.h"

#include <QString>

#import <CoreAudio/CoreAudio.h>
#import <CoreFoundation/CoreFoundation.h>

namespace speecher::mac {

std::optional<int> nominalSampleRate(const QByteArray &deviceUid)
{
    CFStringRef uid = QString::fromUtf8(deviceUid).toCFString();
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const AudioObjectPropertyAddress translate{
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    const OSStatus found = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                      &translate,
                                                      sizeof(uid),
                                                      &uid,
                                                      &size,
                                                      &device);
    CFRelease(uid);
    if (found != noErr || device == kAudioObjectUnknown) {
        return std::nullopt;
    }

    const AudioObjectPropertyAddress rateAddress{
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    Float64 rate = 0;
    size = sizeof(rate);
    if (AudioObjectGetPropertyData(device, &rateAddress, 0, nullptr, &size, &rate) != noErr
        || rate <= 0) {
        return std::nullopt;
    }
    // Truncated, as Qt compares it.
    return int(rate);
}

} // namespace speecher::mac
