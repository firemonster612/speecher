#include "platform/mac/MacSystemAudioInput.h"

#include "platform/mac/MacSystemAudioCapture.h"
#include "recording/SystemAudioPresentation.h"

#include <QMetaObject>
#include <QScopeGuard>

#include <cstring>
#include <map>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace speecher {
namespace {

QString errorText(NSError *error)
{
    return error ? QString::fromNSString(error.localizedDescription)
                 : QStringLiteral("ScreenCaptureKit did not answer.");
}

qint64 msBetween(CMTime from, CMTime to)
{
    return qint64(CMTimeGetSeconds(CMTimeSubtract(to, from)) * 1000);
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

} // namespace

SystemAudioCapture::SystemAudioCapture(QString permissionDenied)
    : m_permissionDenied(std::move(permissionDenied))
{
}

void SystemAudioCapture::open()
{
    // The timeline runs on the host clock, mach_absolute_time, which stops
    // while the machine sleeps like Windows' unbiased interrupt time, so
    // sleep is skipped rather than filled; mach_continuous_time would count
    // it. ScreenCaptureKit stamps sample buffers on the same clock, so a
    // buffer is placed by its stamp alone, with no second clock for
    // loopbackPacketMs to reconcile.
    m_start = hostTime();
    openStream();
}

void SystemAudioCapture::started(quint64 streamId, const StreamStart &start)
{
    // A stream given up on, or that starts after capture ended, is stopped.
    if (!m_attached || streamId != m_streamId) {
        stopStream(streamId);
        return;
    }
    m_startingSinceMs.reset();
    const bool reopening = std::exchange(m_opened, true);
    if (!reopening) {
        opened(start);
    }
    if (start.started) {
        m_runningSinceMs = elapsedMs();
        return;
    }
    stopStream(streamId);
    m_streamId = 0;
    // A first stream that refuses fails start() instead.
    if (reopening) {
        refused(start.error, start.noDisplay ? LoopbackReopen::Refusal::NoOutput : LoopbackReopen::Refusal::Failed);
    }
}

void SystemAudioCapture::read(quint64 streamId, CMSampleBufferRef buffer)
{
    if (!m_attached || streamId != m_streamId) {
        return;
    }
    const SampleBufferPcm pcm = pcmForSampleBuffer(buffer);
    if (!pcm.format.isValid()) {
        fail(systemAudioCaptureStoppedText(QStringLiteral("macOS sent audio in a format Speecher cannot read.")));
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

void SystemAudioCapture::stopped(quint64 streamId, NSError *error)
{
    if (!m_attached || streamId != m_streamId) {
        return;
    }
    // Only a stream that ran for the whole allowance starts it afresh; any
    // other stop counts as a refusal.
    if (!m_startingSinceMs && elapsedMs() - m_runningSinceMs >= LoopbackReopen::kAllowanceMs) {
        m_reopen.restart();
    }
    m_startingSinceMs.reset();
    stopStream(streamId);
    m_streamId = 0;
    refused(error, LoopbackReopen::Refusal::Failed);
}

void SystemAudioCapture::poll()
{
    if (!m_attached) {
        return;
    }
    if (!m_heard) {
        send(m_pcm.silenceUntil(elapsedMs()));
    }
    m_heard = false;
    if (!m_streamId) {
        openStream();
    } else if (m_startingSinceMs && elapsedMs() - *m_startingSinceMs >= kStreamReplyTimeoutMs) {
        // started() stops the stream if it answers after all.
        m_startingSinceMs.reset();
        m_streamId = 0;
        refused(nil, LoopbackReopen::Refusal::Failed);
    }
}

void SystemAudioCapture::detach()
{
    m_attached = false;
}

CMTime SystemAudioCapture::hostTime() const
{
    return CMClockGetTime(CMClockGetHostTimeClock());
}

void SystemAudioCapture::openStream()
{
    m_streamId = ++m_lastStreamId;
    m_startingSinceMs = elapsedMs();
    startStream(m_streamId);
}

void SystemAudioCapture::refused(NSError *error, LoopbackReopen::Refusal refusal)
{
    if (isScreenRecordingDenied((__bridge CFErrorRef)error)) {
        fail(m_permissionDenied);
        return;
    }
    const bool userStopped =
        [error.domain isEqualToString:SCStreamErrorDomain] && error.code == SCStreamErrorUserStopped;
    if (userStopped || !m_reopen.retries(refusal, elapsedMs())) {
        fail(systemAudioCaptureStoppedText(errorText(error)));
    }
}

// Capture ends at the first failure, so nothing is sent after it.
void SystemAudioCapture::fail(const QString &message)
{
    detach();
    sendFailure(message);
}

void SystemAudioCapture::send(const QList<QByteArray> &chunks)
{
    if (!chunks.isEmpty()) {
        sendAudio(chunks);
    }
}

qint64 SystemAudioCapture::elapsedMs() const
{
    return msBetween(m_start, hostTime());
}

} // namespace speecher

// A ScreenCaptureKit stream's output and delegate, which hand everything to
// capture on the capture queue, tagged with the stream's id.
@interface SpeecherSystemAudioOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithCapture:(std::shared_ptr<speecher::SystemAudioCapture>)capture
                       streamId:(quint64)streamId
                          queue:(dispatch_queue_t)queue;
@end

@implementation SpeecherSystemAudioOutput {
    std::shared_ptr<speecher::SystemAudioCapture> _capture;
    quint64 _streamId;
    dispatch_queue_t _queue;
}

- (instancetype)initWithCapture:(std::shared_ptr<speecher::SystemAudioCapture>)capture
                       streamId:(quint64)streamId
                          queue:(dispatch_queue_t)queue
{
    if ((self = [super init])) {
        _capture = std::move(capture);
        _streamId = streamId;
        _queue = queue;
    }
    return self;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)buffer ofType:(SCStreamOutputType)type
{
    // The video frames are dropped here; they only keep ScreenCaptureKit from
    // logging each one it has nowhere to send.
    if (type == SCStreamOutputTypeAudio && CMSampleBufferIsValid(buffer)) {
        _capture->read(_streamId, buffer);
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    const std::shared_ptr<speecher::SystemAudioCapture> capture = _capture;
    const quint64 streamId = _streamId;
    dispatch_async(_queue, ^{
        capture->stopped(streamId, error);
    });
}

@end

namespace speecher {

// Capture from ScreenCaptureKit into the input that opened it. Each stream
// is set up on the capture queue between ScreenCaptureKit's answers, so
// nothing waits for them but start() for the first.
class ScreenCaptureAudio final : public SystemAudioCapture, public std::enable_shared_from_this<ScreenCaptureAudio> {
public:
    ScreenCaptureAudio(MacSystemAudioInput *input, dispatch_queue_t queue)
        : SystemAudioCapture(input->m_permissionDenied)
        , m_input(input)
        , m_generation(input->m_generation)
        , m_queue(queue)
        , m_answered(dispatch_semaphore_create(0))
    {
    }

    // Waits on the main thread for the first stream's answer.
    StreamStart waitUntilOpened()
    {
        // An answer that comes after the wait gave up still writes, so what
        // it wrote is read only once it has answered.
        if (dispatch_semaphore_wait(m_answered,
                                    dispatch_time(DISPATCH_TIME_NOW, int64_t(kStreamReplyTimeoutMs * NSEC_PER_MSEC)))
            != 0) {
            return {};
        }
        return m_firstStart;
    }

    // Stops every stream, once detached.
    void stopStreams()
    {
        while (!m_natives.empty()) {
            stopStream(m_natives.begin()->first);
        }
    }

private:
    void startStream(quint64 streamId) override;
    void stopStream(quint64 streamId) override;
    void opened(const StreamStart &start) override;
    void sendAudio(const QList<QByteArray> &chunks) override;
    void sendFailure(const QString &message) override;
    // Runs on the capture queue once ScreenCaptureKit has listed what there
    // is to capture.
    void startWith(quint64 streamId, SCShareableContent *content, NSError *contentError);

    struct NativeStream {
        SCStream *stream;
        SpeecherSystemAudioOutput *output;
    };

    MacSystemAudioInput *const m_input;
    const quint64 m_generation;
    const dispatch_queue_t m_queue;
    const dispatch_semaphore_t m_answered;
    StreamStart m_firstStart;
    std::map<quint64, NativeStream> m_natives;
};

void ScreenCaptureAudio::startStream(quint64 streamId)
{
    const std::shared_ptr<ScreenCaptureAudio> capture = shared_from_this();
    dispatch_queue_t queue = m_queue;
    // Asks for the permission the first time, and fails while it is off.
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent *content, NSError *error) {
                                                     dispatch_async(queue, ^{
                                                         capture->startWith(streamId, content, error);
                                                     });
                                                 }];
}

void ScreenCaptureAudio::startWith(quint64 streamId, SCShareableContent *content, NSError *contentError)
{
    if (!content) {
        started(streamId, {.error = contentError});
        return;
    }
    SCDisplay *display = mainDisplay(content);
    if (!display) {
        started(streamId, {.noDisplay = true});
        return;
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
    SpeecherSystemAudioOutput *output = [[SpeecherSystemAudioOutput alloc] initWithCapture:shared_from_this()
                                                                                  streamId:streamId
                                                                                     queue:m_queue];
    SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:output];
    NSError *outputError = nil;
    if (![stream addStreamOutput:output type:SCStreamOutputTypeAudio sampleHandlerQueue:m_queue error:&outputError]
        || ![stream addStreamOutput:output
                               type:SCStreamOutputTypeScreen
                 sampleHandlerQueue:m_queue
                              error:&outputError]) {
        started(streamId, {.error = outputError});
        return;
    }

    // Kept before it starts, so a start that answers late is still stopped.
    m_natives[streamId] = {stream, output};
    const std::shared_ptr<ScreenCaptureAudio> capture = shared_from_this();
    dispatch_queue_t queue = m_queue;
    [stream startCaptureWithCompletionHandler:^(NSError *error) {
        dispatch_async(queue, ^{
            capture->started(streamId, {.started = !error, .error = error});
        });
    }];
}

void ScreenCaptureAudio::stopStream(quint64 streamId)
{
    const auto native = m_natives.find(streamId);
    if (native == m_natives.end()) {
        return;
    }
    SCStream *stream = native->second.stream;
    SpeecherSystemAudioOutput *output = native->second.output;
    m_natives.erase(native);
    // The handler keeps the stream and its output until it has stopped.
    [stream stopCaptureWithCompletionHandler:^(NSError *) {
        (void)stream;
        (void)output;
    }];
}

void ScreenCaptureAudio::opened(const StreamStart &start)
{
    m_firstStart = start;
    dispatch_semaphore_signal(m_answered);
}

void ScreenCaptureAudio::sendAudio(const QList<QByteArray> &chunks)
{
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

void ScreenCaptureAudio::sendFailure(const QString &message)
{
    MacSystemAudioInput *input = m_input;
    const quint64 generation = m_generation;
    QMetaObject::invokeMethod(
        input, [input, message, generation] { input->fail(generation, message); }, Qt::QueuedConnection);
}

struct MacSystemAudioInput::Stream {
    dispatch_queue_t queue;
    std::shared_ptr<ScreenCaptureAudio> capture;
    dispatch_source_t poll = nil;
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
    auto capture = std::make_shared<ScreenCaptureAudio>(this, queue);
    m_stream.reset(new Stream{queue, capture});
    dispatch_async(queue, ^{
        capture->open();
    });
    const StreamStart start = capture->waitUntilOpened();
    if (start.noDisplay) {
        return systemAudioCaptureFailedText(
            QStringLiteral("macOS captures it only alongside a display, and there is none."));
    }
    if (!start.started) {
        return isScreenRecordingDenied((__bridge CFErrorRef)start.error)
            ? m_permissionDenied
            : systemAudioCaptureFailedText(errorText(start.error));
    }

    // Polled as well, because nothing arrives while nothing plays, and the
    // poll that finds nothing is what fills the silence.
    m_stream->poll = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    const uint64_t interval = kLoopbackPollMs * NSEC_PER_MSEC;
    dispatch_source_set_timer(m_stream->poll, dispatch_time(DISPATCH_TIME_NOW, int64_t(interval)), interval,
                              interval / 10);
    dispatch_source_set_event_handler(m_stream->poll, ^{
        capture->poll();
    });
    dispatch_resume(m_stream->poll);
    return {};
}

void MacSystemAudioInput::stop()
{
    if (!m_stream) {
        return;
    }
    const std::unique_ptr<Stream> stream = std::move(m_stream);
    ++m_generation;
    // Once this has run on the capture queue, nothing there reaches this
    // input again, and ScreenCaptureKit stops the streams in the background.
    const std::shared_ptr<ScreenCaptureAudio> capture = stream->capture;
    dispatch_source_t poll = stream->poll;
    dispatch_sync(stream->queue, ^{
        capture->detach();
        capture->stopStreams();
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
