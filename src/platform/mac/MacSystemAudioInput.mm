#include "platform/mac/MacSystemAudioInput.h"

#include "platform/audio/LoopbackPcm.h"
#include "platform/audio/LoopbackReopen.h"

#include <QMetaObject>
#include <QScopeGuard>
#include <QTimer>

#include <cstring>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace speecher {
namespace {

// How long start and stop wait for ScreenCaptureKit, which answers on a queue
// of its own.
constexpr int64_t kReplyTimeoutNs = 10 * NSEC_PER_SEC;

bool waitForReply(dispatch_semaphore_t replied)
{
    return dispatch_semaphore_wait(replied, dispatch_time(DISPATCH_TIME_NOW, kReplyTimeoutNs)) == 0;
}

QString errorText(NSError *error)
{
    return error ? QString::fromNSString(error.localizedDescription)
                 : QStringLiteral("ScreenCaptureKit did not answer.");
}

qint64 msBetween(CMTime from, CMTime to)
{
    return qint64(CMTimeGetSeconds(CMTimeSubtract(to, from)) * 1000);
}

qint64 hostClockMs()
{
    return qint64(CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock())) * 1000);
}

// The display to capture alongside: the main one, with the menu bar, when
// ScreenCaptureKit lists it.
SCDisplay *mainDisplay(SCShareableContent *content)
{
    for (SCDisplay *display in content.displays) {
        if (display.displayID == CGMainDisplayID()) {
            return display;
        }
    }
    return content.displays.firstObject;
}

// Waits for ScreenCaptureKit to stop stream, when there is one.
void stopCapture(SCStream *stream)
{
    if (!stream) {
        return;
    }
    dispatch_semaphore_t stopped = dispatch_semaphore_create(0);
    [stream stopCaptureWithCompletionHandler:^(NSError *) {
        dispatch_semaphore_signal(stopped);
    }];
    waitForReply(stopped);
}

} // namespace

// What runs on the capture queue: the stream's timeline, and the input it
// sends audio to until stop() detaches it there.
class SystemAudioReader {
public:
    SystemAudioReader(MacSystemAudioInput *input, quint64 generation, QString permissionDenied)
        : m_input(input)
        , m_generation(generation)
        , m_permissionDenied(std::move(permissionDenied))
        // The timeline runs on the host clock, mach_absolute_time, which
        // stops while the machine sleeps like Windows' unbiased interrupt
        // time, so sleep is skipped rather than filled; mach_continuous_time
        // would count it. ScreenCaptureKit stamps sample buffers on the same
        // clock, so a buffer is placed by its stamp alone, with no second
        // clock for loopbackPacketMs to reconcile.
        , m_start(CMClockGetTime(CMClockGetHostTimeClock()))
    {
    }

    void read(CMSampleBufferRef buffer)
    {
        if (!m_input) {
            return;
        }
        const SampleBufferPcm pcm = pcmForSampleBuffer(buffer);
        if (!pcm.format.isValid()) {
            fail(QStringLiteral("System audio capture stopped: macOS sent audio in a format Speecher cannot read."));
            return;
        }
        if (pcm.data.isEmpty()) {
            return;
        }
        if (pcm.format != m_format) {
            m_format = pcm.format;
            m_pcm.useFormat(pcm.format);
        }
        QList<QByteArray> chunks;
        const CMTime playedAt = CMSampleBufferGetPresentationTimeStamp(buffer);
        if (CMTIME_IS_NUMERIC(playedAt)) {
            // Audio cannot have played after now, so a stamp that says it did,
            // from a clock that jumped, is taken as now: one bad stamp adds no
            // more silence than a poll would.
            chunks = m_pcm.silenceUntil(qMin(msBetween(m_start, playedAt), elapsedMs()));
        }
        if (const QByteArray converted = m_pcm.convert(pcm.data); !converted.isEmpty()) {
            chunks.append(converted);
        }
        m_heard = true;
        send(chunks);
    }

    // Fills the time since the last poll with silence if no audio came in it.
    void poll()
    {
        if (!m_input) {
            return;
        }
        if (!m_heard) {
            send(m_pcm.silenceUntil(elapsedMs()));
        }
        m_heard = false;
    }

    // A stream that stopped for anything but the permission or the user, such
    // as its display going away, is opened again while the timeline carries
    // on.
    void stopped(NSError *error)
    {
        if (!m_input) {
            return;
        }
        if (isScreenRecordingDenied((__bridge CFErrorRef)error)) {
            fail(m_permissionDenied);
            return;
        }
        if ([error.domain isEqualToString:SCStreamErrorDomain] && error.code == SCStreamErrorUserStopped) {
            fail(QStringLiteral("System audio capture stopped: %1").arg(errorText(error)));
            return;
        }
        MacSystemAudioInput *input = m_input;
        const quint64 generation = m_generation;
        QMetaObject::invokeMethod(input, [input, generation] { input->reopen(generation); }, Qt::QueuedConnection);
    }

    void detach() { m_input = nullptr; }

private:
    qint64 elapsedMs() const { return msBetween(m_start, CMClockGetTime(CMClockGetHostTimeClock())); }

    void send(const QList<QByteArray> &chunks)
    {
        if (!m_input || chunks.isEmpty()) {
            return;
        }
        MacSystemAudioInput *input = m_input;
        const quint64 generation = m_generation;
        QMetaObject::invokeMethod(
            input,
            [input, chunks, generation] {
                for (const QByteArray &chunk : chunks) {
                    if (generation != input->m_generation) {
                        return;
                    }
                    emit input->audioChunk(chunk);
                }
            },
            Qt::QueuedConnection);
    }

    // Capture ends at the first failure, so nothing is sent after it.
    void fail(const QString &message)
    {
        if (!m_input) {
            return;
        }
        MacSystemAudioInput *input = m_input;
        const quint64 generation = m_generation;
        QMetaObject::invokeMethod(
            input, [input, message, generation] { input->fail(generation, message); }, Qt::QueuedConnection);
        m_input = nullptr;
    }

    MacSystemAudioInput *m_input;
    const quint64 m_generation;
    const QString m_permissionDenied;
    const CMTime m_start;
    LoopbackPcm m_pcm;
    QAudioFormat m_format;
    // Whether audio came in since the last poll.
    bool m_heard = false;
};

} // namespace speecher

// ScreenCaptureKit's output and delegate, which hand everything to the reader
// on the capture queue.
@interface SpeecherSystemAudioOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithReader:(std::shared_ptr<speecher::SystemAudioReader>)reader queue:(dispatch_queue_t)queue;
@end

@implementation SpeecherSystemAudioOutput {
    std::shared_ptr<speecher::SystemAudioReader> _reader;
    dispatch_queue_t _queue;
}

- (instancetype)initWithReader:(std::shared_ptr<speecher::SystemAudioReader>)reader queue:(dispatch_queue_t)queue
{
    if ((self = [super init])) {
        _reader = std::move(reader);
        _queue = queue;
    }
    return self;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)buffer ofType:(SCStreamOutputType)type
{
    // The video frames are dropped here; they only keep ScreenCaptureKit from
    // logging each one it has nowhere to send.
    if (type == SCStreamOutputTypeAudio && CMSampleBufferIsValid(buffer)) {
        _reader->read(buffer);
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    const std::shared_ptr<speecher::SystemAudioReader> reader = _reader;
    dispatch_async(_queue, ^{
        reader->stopped(error);
    });
}

@end

namespace speecher {

struct MacSystemAudioInput::Stream {
    dispatch_queue_t queue;
    std::shared_ptr<SystemAudioReader> reader;
    SpeecherSystemAudioOutput *output;
    SCStream *stream = nil;
    dispatch_source_t poll = nil;
    LoopbackReopen reopen;
};

struct MacSystemAudioInput::StreamStart {
    bool started = false;
    // Why it did not: there is no display to capture alongside, or what
    // ScreenCaptureKit said, nil when it did not answer.
    bool noDisplay = false;
    NSError *error = nil;
};

// ScreenCaptureKit is asked for 16 kHz mono float, so the other layouts read
// here are a guard against it delivering something else on some macOS
// release rather than failing capture there; the converter takes any rate.
SampleBufferPcm pcmForSampleBuffer(CMSampleBufferRef buffer)
{
    const AudioStreamBasicDescription *description =
        CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(buffer));
    if (!description || description->mFormatID != kAudioFormatLinearPCM
        || (description->mFormatFlags & kAudioFormatFlagIsBigEndian)) {
        return {};
    }
    const AudioFormatFlags flags = description->mFormatFlags;
    QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Unknown;
    if ((flags & kAudioFormatFlagIsFloat) && description->mBitsPerChannel == 32) {
        sampleFormat = QAudioFormat::Float;
    } else if ((flags & kAudioFormatFlagIsSignedInteger) && description->mBitsPerChannel == 16) {
        sampleFormat = QAudioFormat::Int16;
    } else if ((flags & kAudioFormatFlagIsSignedInteger) && description->mBitsPerChannel == 32) {
        sampleFormat = QAudioFormat::Int32;
    }
    SampleBufferPcm pcm;
    pcm.format.setSampleRate(int(description->mSampleRate));
    pcm.format.setChannelCount(int(description->mChannelsPerFrame));
    pcm.format.setSampleFormat(sampleFormat);
    const bool planar = flags & kAudioFormatFlagIsNonInterleaved;
    const int channels = pcm.format.channelCount();
    const int sampleBytes = pcm.format.bytesPerSample();
    // Padded frames would be misread as samples.
    if (int(description->mBytesPerFrame) != (planar ? sampleBytes : pcm.format.bytesPerFrame())) {
        return {};
    }
    if (!pcm.format.isValid()) {
        return {};
    }
    // A buffer whose samples are not there yet, or that has none, is skipped
    // rather than taken for a format Speecher cannot read.
    if (!CMSampleBufferDataIsReady(buffer) || CMSampleBufferGetNumSamples(buffer) == 0) {
        return pcm;
    }

    size_t listSize = 0;
    constexpr uint32_t aligned = kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            buffer, &listSize, nullptr, 0, nullptr, nullptr, aligned, nullptr)
        != noErr) {
        return {};
    }
    QByteArray listStorage(qsizetype(listSize), '\0');
    auto *list = reinterpret_cast<AudioBufferList *>(listStorage.data());
    CMBlockBufferRef block = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            buffer, nullptr, list, listSize, nullptr, nullptr, aligned, &block)
            != noErr
        || !block) {
        return {};
    }
    const auto releaseBlock = qScopeGuard([block] { CFRelease(block); });
    if (list->mNumberBuffers != UInt32(planar ? channels : 1)) {
        return {};
    }
    if (!planar || channels == 1) {
        pcm.data = QByteArray(static_cast<const char *>(list->mBuffers[0].mData),
                              qsizetype(list->mBuffers[0].mDataByteSize));
        return pcm;
    }
    // One buffer a channel, which the converter reads interleaved.
    const qsizetype frames = list->mBuffers[0].mDataByteSize / UInt32(sampleBytes);
    pcm.data.resize(frames * channels * sampleBytes);
    for (int channel = 0; channel < channels; ++channel) {
        const AudioBuffer &source = list->mBuffers[channel];
        if (source.mDataByteSize / UInt32(sampleBytes) < frames) {
            return {};
        }
        const auto *from = static_cast<const char *>(source.mData);
        for (qsizetype frame = 0; frame < frames; ++frame) {
            memcpy(pcm.data.data() + (frame * channels + channel) * sampleBytes, from + frame * sampleBytes,
                   size_t(sampleBytes));
        }
    }
    return pcm;
}

bool isScreenRecordingDenied(CFErrorRef error)
{
    NSError *nsError = (__bridge NSError *)error;
    return [nsError.domain isEqualToString:SCStreamErrorDomain] && nsError.code == SCStreamErrorUserDeclined;
}

MacSystemAudioInput::MacSystemAudioInput(QString permissionDenied, QObject *parent)
    : AudioInput(parent)
    , m_permissionDenied(std::move(permissionDenied))
{
}

MacSystemAudioInput::~MacSystemAudioInput()
{
    stop();
}

bool MacSystemAudioInput::start(QString *error)
{
    if (m_stream) {
        return true;
    }
    const QString message = open();
    if (message.isEmpty()) {
        return true;
    }
    stop();
    if (error) {
        *error = message;
    }
    return false;
}

QString MacSystemAudioInput::open()
{
    auto queue = dispatch_queue_create("io.github.firemonster612.speecher.system-audio", DISPATCH_QUEUE_SERIAL);
    auto reader = std::make_shared<SystemAudioReader>(this, m_generation, m_permissionDenied);
    m_stream.reset(new Stream{queue, reader, [[SpeecherSystemAudioOutput alloc] initWithReader:reader queue:queue]});
    const StreamStart start = startStream();
    if (start.noDisplay) {
        return QStringLiteral("Could not capture system audio: macOS captures it only alongside a display, and there "
                              "is none.");
    }
    if (!start.started) {
        return isScreenRecordingDenied((__bridge CFErrorRef)start.error)
            ? m_permissionDenied
            : QStringLiteral("Could not capture system audio: %1").arg(errorText(start.error));
    }

    // Polled as well, because nothing arrives while nothing plays, and the
    // poll that finds nothing is what fills the silence.
    m_stream->poll = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    const uint64_t interval = kLoopbackPollMs * NSEC_PER_MSEC;
    dispatch_source_set_timer(m_stream->poll, dispatch_time(DISPATCH_TIME_NOW, int64_t(interval)), interval,
                              interval / 10);
    dispatch_source_set_event_handler(m_stream->poll, ^{
        reader->poll();
    });
    dispatch_resume(m_stream->poll);
    return {};
}

MacSystemAudioInput::StreamStart MacSystemAudioInput::startStream()
{
    // Asks for the permission the first time, and fails while it is off.
    __block SCShareableContent *content = nil;
    __block NSError *contentError = nil;
    dispatch_semaphore_t answered = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent *shareable, NSError *error) {
                                                     content = shareable;
                                                     contentError = error;
                                                     dispatch_semaphore_signal(answered);
                                                 }];
    // A handler that answers after the wait gave up still writes, so what it
    // wrote is read only once it has answered.
    if (!waitForReply(answered)) {
        return {};
    }
    if (!content) {
        return {.error = contentError};
    }
    SCDisplay *display = mainDisplay(content);
    if (!display) {
        return {.noDisplay = true};
    }

    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.capturesAudio = YES;
    configuration.excludesCurrentProcessAudio = YES;
    configuration.sampleRate = 16000;
    configuration.channelCount = 1;
    configuration.width = 2;
    configuration.height = 2;
    configuration.minimumFrameInterval = CMTimeMake(1, 1);
    configuration.showsCursor = NO;

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:m_stream->output];
    NSError *outputError = nil;
    if (![stream addStreamOutput:m_stream->output
                            type:SCStreamOutputTypeAudio
              sampleHandlerQueue:m_stream->queue
                           error:&outputError]
        || ![stream addStreamOutput:m_stream->output
                               type:SCStreamOutputTypeScreen
                 sampleHandlerQueue:m_stream->queue
                              error:&outputError]) {
        return {.error = outputError};
    }

    __block NSError *startError = nil;
    dispatch_semaphore_t started = dispatch_semaphore_create(0);
    // Kept before it starts, so a start that answers late is still stopped.
    m_stream->stream = stream;
    [stream startCaptureWithCompletionHandler:^(NSError *error) {
        startError = error;
        dispatch_semaphore_signal(started);
    }];
    if (!waitForReply(started)) {
        return {};
    }
    if (startError) {
        return {.error = startError};
    }
    return {.started = true};
}

void MacSystemAudioInput::reopen(quint64 generation)
{
    if (generation != m_generation) {
        return;
    }
    stopCapture(m_stream->stream);
    m_stream->stream = nil;
    const StreamStart start = startStream();
    if (start.started) {
        m_stream->reopen.restart();
        return;
    }
    if (isScreenRecordingDenied((__bridge CFErrorRef)start.error)) {
        fail(generation, m_permissionDenied);
        return;
    }
    const auto refusal = start.noDisplay ? LoopbackReopen::Refusal::NoOutput : LoopbackReopen::Refusal::Failed;
    if (m_stream->reopen.retries(refusal, hostClockMs())) {
        QTimer::singleShot(kLoopbackPollMs, this, [this, generation] { reopen(generation); });
        return;
    }
    fail(generation, QStringLiteral("System audio capture stopped: %1").arg(errorText(start.error)));
}

void MacSystemAudioInput::stop()
{
    if (!m_stream) {
        return;
    }
    const std::unique_ptr<Stream> stream = std::move(m_stream);
    ++m_generation;
    stopCapture(stream->stream);
    // Once this has run on the capture queue, nothing there reaches this
    // input again.
    const std::shared_ptr<SystemAudioReader> reader = stream->reader;
    dispatch_source_t poll = stream->poll;
    dispatch_sync(stream->queue, ^{
        reader->detach();
        if (poll) {
            dispatch_source_cancel(poll);
        }
    });
}

bool MacSystemAudioInput::isActive() const
{
    return m_stream != nullptr;
}

void MacSystemAudioInput::fail(quint64 generation, const QString &message)
{
    if (generation != m_generation) {
        return;
    }
    stop();
    emit failed(message);
}

} // namespace speecher
