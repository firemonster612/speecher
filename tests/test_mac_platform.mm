#include "common/test_suites.h"

#include "platform/mac/MacSystemAudioInput.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

using namespace speecher;

namespace {

template<typename T>
QByteArray bytes(std::initializer_list<T> values)
{
    QByteArray result;
    for (const T value : values) {
        result.append(reinterpret_cast<const char *>(&value), sizeof(T));
    }
    return result;
}

AudioStreamBasicDescription floatDescription(double sampleRate, UInt32 channels, bool planar)
{
    AudioStreamBasicDescription description{};
    description.mSampleRate = sampleRate;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked
        | (planar ? kAudioFormatFlagIsNonInterleaved : 0);
    description.mFramesPerPacket = 1;
    description.mChannelsPerFrame = channels;
    description.mBitsPerChannel = 32;
    description.mBytesPerFrame = planar ? 4 : 4 * channels;
    description.mBytesPerPacket = description.mBytesPerFrame;
    return description;
}

bool isPlanar(const AudioStreamBasicDescription &description)
{
    return description.mFormatFlags & kAudioFormatFlagIsNonInterleaved;
}

// An audio sample buffer as ScreenCaptureKit hands one over, holding buffers,
// one a channel when the description is planar.
CMSampleBufferRef sampleBuffer(const AudioStreamBasicDescription &description,
                               CMItemCount frames,
                               const QList<QByteArray> &buffers)
{
    CMAudioFormatDescriptionRef format = nullptr;
    CMAudioFormatDescriptionCreate(nullptr, &description, 0, nullptr, 0, nullptr, nullptr, &format);
    const auto releaseFormat = qScopeGuard([format] { CFRelease(format); });
    CMSampleBufferRef buffer = nullptr;
    CMAudioSampleBufferCreateWithPacketDescriptions(
        nullptr, nullptr, false, nullptr, nullptr, format, frames, kCMTimeZero, nullptr, &buffer);
    QByteArray listStorage(qsizetype(offsetof(AudioBufferList, mBuffers) + sizeof(AudioBuffer) * buffers.size()),
                           '\0');
    auto *list = reinterpret_cast<AudioBufferList *>(listStorage.data());
    list->mNumberBuffers = UInt32(buffers.size());
    for (qsizetype index = 0; index < buffers.size(); ++index) {
        list->mBuffers[index] = {isPlanar(description) ? 1 : description.mChannelsPerFrame,
                                 UInt32(buffers.at(index).size()),
                                 const_cast<char *>(buffers.at(index).constData())};
    }
    CMSampleBufferSetDataBufferFromAudioBufferList(
        buffer, nullptr, nullptr, kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, list);
    return buffer;
}

class MacPlatformTests final : public QObject {
    Q_OBJECT

private slots:
    void systemAudioInterleavesPlanarBuffers()
    {
        // ScreenCaptureKit's own layout: float, one buffer a channel.
        CMSampleBufferRef buffer = sampleBuffer(floatDescription(48000, 2, true),
                                                3,
                                                {bytes<float>({0.25f, 0.5f, 0.75f}), bytes<float>({-0.25f, -0.5f, -0.75f})});
        QVERIFY(buffer);
        const auto release = qScopeGuard([buffer] { CFRelease(buffer); });

        const SampleBufferPcm pcm = pcmForSampleBuffer(buffer);
        QCOMPARE(pcm.format.sampleFormat(), QAudioFormat::Float);
        QCOMPARE(pcm.format.sampleRate(), 48000);
        QCOMPARE(pcm.format.channelCount(), 2);
        QCOMPARE(pcm.data, bytes<float>({0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f}));
    }

    void systemAudioReadsTheMonoItAsksFor()
    {
        const QByteArray samples = bytes<float>({0.1f, 0.2f, 0.3f, 0.4f});
        CMSampleBufferRef buffer = sampleBuffer(floatDescription(16000, 1, true), 4, {samples});
        QVERIFY(buffer);
        const auto release = qScopeGuard([buffer] { CFRelease(buffer); });

        const SampleBufferPcm pcm = pcmForSampleBuffer(buffer);
        QCOMPARE(pcm.format.sampleRate(), 16000);
        QCOMPARE(pcm.format.channelCount(), 1);
        QCOMPARE(pcm.data, samples);
    }

    void systemAudioRefusesFormatsItCannotRead()
    {
        // Packed 24-bit samples, which the converter has no reader for.
        AudioStreamBasicDescription packed24{};
        packed24.mSampleRate = 48000;
        packed24.mFormatID = kAudioFormatLinearPCM;
        packed24.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        packed24.mFramesPerPacket = 1;
        packed24.mChannelsPerFrame = 2;
        packed24.mBitsPerChannel = 24;
        packed24.mBytesPerFrame = 6;
        packed24.mBytesPerPacket = 6;
        CMSampleBufferRef buffer = sampleBuffer(packed24, 1, {QByteArray(6, '\0')});
        QVERIFY(buffer);
        const auto release = qScopeGuard([buffer] { CFRelease(buffer); });

        QVERIFY(!pcmForSampleBuffer(buffer).format.isValid());
    }

    void screenRecordingDenialIsRecognized()
    {
        NSError *declined = [NSError errorWithDomain:SCStreamErrorDomain code:SCStreamErrorUserDeclined userInfo:nil];
        QVERIFY(isScreenRecordingDenied((__bridge CFErrorRef)declined));

        NSError *stopped = [NSError errorWithDomain:SCStreamErrorDomain code:SCStreamErrorUserStopped userInfo:nil];
        QVERIFY(!isScreenRecordingDenied((__bridge CFErrorRef)stopped));
        NSError *elsewhere = [NSError errorWithDomain:NSOSStatusErrorDomain code:SCStreamErrorUserDeclined userInfo:nil];
        QVERIFY(!isScreenRecordingDenied((__bridge CFErrorRef)elsewhere));
    }

    void systemAudioStartsOrSaysWhy()
    {
        const QString denied = QStringLiteral("permission denied");
        MacSystemAudioInput capture(denied);
        QByteArray pcm;
        connect(&capture, &AudioInput::audioChunk, &capture, [&](const QByteArray &chunk) { pcm += chunk; });
        QSignalSpy failed(&capture, &AudioInput::failed);
        QString error;
        if (capture.start(&error)) {
            QVERIFY(capture.isActive());
            // With or without anything playing, the stream keeps time.
            QTRY_VERIFY(pcm.size() >= 16000 * 2 / 2);
            capture.stop();
        } else {
            // Runners without Screen & System Audio Recording end here.
            qInfo().noquote() << "system audio did not start:" << error;
            QVERIFY(error == denied || error.startsWith(QStringLiteral("Could not capture system audio: ")));
        }
        QVERIFY(!capture.isActive());
        // A failed start is reported once, by its return value.
        QTest::qWait(300);
        QCOMPARE(failed.count(), 0);
    }
};

} // namespace

int runMacPlatformTests(int argc, char **argv)
{
    MacPlatformTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_mac_platform.moc"
