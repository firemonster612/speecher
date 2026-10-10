#include "common/test_suites.h"

#include "platform/mac/MacSystemAudioCapture.h"
#include "platform/mac/MacSystemAudioInput.h"

#include <QScopeGuard>
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
// one a channel when the description is planar, or with its data not there
// yet when there are none, stamped playedAt.
CMSampleBufferRef sampleBuffer(const AudioStreamBasicDescription &description,
                               CMItemCount frames,
                               const QList<QByteArray> &buffers,
                               CMTime playedAt = kCMTimeZero)
{
    CMAudioFormatDescriptionRef format = nullptr;
    CMAudioFormatDescriptionCreate(nullptr, &description, 0, nullptr, 0, nullptr, nullptr, &format);
    const auto releaseFormat = qScopeGuard([format] { CFRelease(format); });
    CMSampleBufferRef buffer = nullptr;
    CMAudioSampleBufferCreateWithPacketDescriptions(
        nullptr, nullptr, false, nullptr, nullptr, format, frames, playedAt, nullptr, &buffer);
    if (buffers.isEmpty()) {
        return buffer;
    }
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

// Capture's 16 kHz mono s16.
constexpr qsizetype kBytesPerMs = 32;

// The host clock's time at the start of a test, so its times are exact.
const CMTime kTestStart = CMTimeMake(1000, 1);

CMTime atMs(qint64 ms)
{
    return CMTimeAdd(kTestStart, CMTimeMake(ms, 1000));
}

// System audio capture with streams that start and stop when the test says,
// on a clock it sets.
class FakeCapture final : public SystemAudioCapture {
public:
    FakeCapture()
        : SystemAudioCapture(QStringLiteral("Screen recording is off."))
    {
    }

    // Starts the first stream and answers that it started.
    void openStarted()
    {
        open();
        started(startedStreams.last(), {.started = true});
    }

    // At ms, the current stream stops, and the poll after it opens another,
    // which starts.
    void stopAndReopen(qint64 ms, NSError *error)
    {
        nowMs = ms;
        stopped(startedStreams.last(), error);
        poll();
        started(startedStreams.last(), {.started = true});
    }

    qint64 nowMs = 0;
    QList<quint64> startedStreams;
    QList<quint64> stoppedStreams;
    QByteArray audio;
    QStringList failures;

private:
    void startStream(quint64 streamId) override { startedStreams.append(streamId); }
    void stopStream(quint64 streamId) override { stoppedStreams.append(streamId); }
    void opened(const StreamStart &) override {}
    void sendAudio(const QList<QByteArray> &chunks) override
    {
        for (const QByteArray &chunk : chunks) {
            audio += chunk;
        }
    }
    void sendFailure(const QString &message) override { failures.append(message); }
    CMTime hostTime() const override { return atMs(nowMs); }
};

NSError *interrupted()
{
    return [NSError errorWithDomain:NSOSStatusErrorDomain
                               code:-1
                           userInfo:@{NSLocalizedDescriptionKey: @"The display went away."}];
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

    void systemAudioSkipsBuffersWithoutData()
    {
        CMSampleBufferRef buffer = sampleBuffer(floatDescription(16000, 1, true), 4, {});
        QVERIFY(buffer);
        const auto release = qScopeGuard([buffer] { CFRelease(buffer); });

        const SampleBufferPcm pcm = pcmForSampleBuffer(buffer);
        QVERIFY(pcm.format.isValid());
        QVERIFY(pcm.data.isEmpty());
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

    void systemAudioGivesUpOnStreamsThatKeepStopping()
    {
        FakeCapture capture;
        capture.openStarted();
        capture.stopAndReopen(1000, interrupted());
        // A stream that ran for the whole allowance does not count the
        // refusals before it.
        capture.stopAndReopen(30000, interrupted());
        QCOMPARE(capture.startedStreams, QList<quint64>({1, 2, 3}));
        QVERIFY(capture.failures.isEmpty());

        // Streams that start and soon stop again fail capture once the
        // allowance has gone by.
        for (qint64 ms = 31000; ms <= 60000; ms += 1000) {
            capture.stopAndReopen(ms, interrupted());
        }
        QCOMPARE(capture.failures, QStringList{QStringLiteral("System audio capture stopped: The display went away.")});
        QCOMPARE(capture.startedStreams.size(), 12);
    }

    void systemAudioIgnoresStreamsItReplaced()
    {
        FakeCapture capture;
        capture.openStarted();
        capture.stopAndReopen(1000, interrupted());

        // Stream 1 reporting its stop again leaves stream 2 running.
        capture.stopped(1, interrupted());
        capture.poll();
        QCOMPARE(capture.startedStreams, QList<quint64>({1, 2}));
        QCOMPARE(capture.stoppedStreams, QList<quint64>({1}));
        QVERIFY(capture.failures.isEmpty());

        // A start ScreenCaptureKit takes too long to answer is given up on,
        // and the stream stopped if it starts after all.
        capture.nowMs = 2000;
        capture.stopped(2, interrupted());
        capture.poll();
        capture.nowMs = 12000;
        capture.poll();
        QCOMPARE(capture.failures,
                 QStringList{QStringLiteral("System audio capture stopped: ScreenCaptureKit did not answer.")});
        capture.started(3, {.started = true});
        QCOMPARE(capture.stoppedStreams, QList<quint64>({1, 2, 3}));
    }

    void systemAudioStopCancelsAReopen()
    {
        FakeCapture capture;
        capture.openStarted();
        capture.nowMs = 1000;
        capture.stopped(1, interrupted());
        capture.detach();
        capture.poll();
        QCOMPARE(capture.startedStreams, QList<quint64>({1}));
        QVERIFY(capture.failures.isEmpty());
    }

    void systemAudioTimelineCarriesOnAcrossAReopen()
    {
        FakeCapture capture;
        capture.openStarted();
        capture.nowMs = 250;
        capture.poll();
        capture.stopAndReopen(500, interrupted());
        QCOMPARE(capture.audio, QByteArray(500 * kBytesPerMs, '\0'));

        capture.nowMs = 750;
        const auto description = floatDescription(16000, 1, true);
        const QList<float> values(160, 0.5f);
        const QByteArray samples(reinterpret_cast<const char *>(values.constData()),
                                 values.size() * qsizetype(sizeof(float)));
        CMSampleBufferRef stale = sampleBuffer(description, 160, {samples}, atMs(750));
        CMSampleBufferRef current = sampleBuffer(description, 160, {samples}, atMs(750));
        QVERIFY(stale && current);
        const auto release = qScopeGuard([stale, current] {
            CFRelease(stale);
            CFRelease(current);
        });
        capture.read(1, stale);
        QCOMPARE(capture.audio.size(), 500 * kBytesPerMs);
        // The new stream's audio is placed by when it played since capture
        // started, after the silence up to it.
        capture.read(2, current);
        QCOMPARE(capture.audio.left(750 * kBytesPerMs), QByteArray(750 * kBytesPerMs, '\0'));
        QVERIFY(capture.audio.size() > 750 * kBytesPerMs);
    }
};

} // namespace

int runMacPlatformTests(int argc, char **argv)
{
    MacPlatformTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_mac_platform.moc"
