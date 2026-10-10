#include "common/test_suites.h"
#include "platform/audio/AudioPcmConverter.h"
#include "platform/audio/LoopbackPcm.h"
#include "platform/audio/LoopbackReopen.h"
#include "platform/mac/MacMicrophoneInput.h"
#include "recording/RecordingPresentation.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <thread>

using namespace speecher;

#include <limits>

namespace {

template<typename T>
QByteArray bytes(std::initializer_list<T> values)
{
    QByteArray result;
    result.reserve(int(values.size() * sizeof(T)));
    for (const T value : values) {
        result.append(reinterpret_cast<const char *>(&value), int(sizeof(value)));
    }
    return result;
}

QByteArray joined(const QList<QByteArray> &chunks)
{
    return chunks.join();
}

QAudioFormat format(int sampleRate, int channels, QAudioFormat::SampleFormat sampleFormat)
{
    QAudioFormat result;
    result.setSampleRate(sampleRate);
    result.setChannelCount(channels);
    result.setSampleFormat(sampleFormat);
    return result;
}

// Stands in for CoreAudio: hands over chunk from a thread of its own every
// 10 ms, from its open until its destruction, counting what it handed over.
class FakeMicrophoneCapture final : public MacMicrophoneCapture {
public:
    FakeMicrophoneCapture(QAudioFormat format, QByteArray chunk, std::atomic_int *handedOver)
        : m_format(format)
        , m_chunk(std::move(chunk))
        , m_handedOver(handedOver)
    {
    }

    ~FakeMicrophoneCapture() override
    {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    QString open(Deliver deliver, Fail fail) override
    {
        fails = std::move(fail);
        m_thread = std::thread([this, deliver = std::move(deliver)] {
            while (m_running) {
                deliver(m_chunk);
                ++*m_handedOver;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        return {};
    }

    QAudioFormat format() const override { return m_format; }

    Fail fails;

private:
    QAudioFormat m_format;
    QByteArray m_chunk;
    std::atomic_int *m_handedOver;
    std::atomic_bool m_running = true;
    std::thread m_thread;
};

class AudioPcmConverterTests : public QObject {
    Q_OBJECT

private slots:
    void convertsSupportedSampleFormats()
    {
        struct Case {
            QAudioFormat::SampleFormat format;
            QByteArray input;
            QByteArray expected;
        };
        const QList<Case> cases{
            {QAudioFormat::UInt8, QByteArray::fromRawData("\0\x80\xff", 3), bytes<qint16>({-32768, 0, 32511})},
            {QAudioFormat::Int16, bytes<qint16>({-32768, 0, 32767}), bytes<qint16>({-32768, 0, 32767})},
            {QAudioFormat::Int32, bytes<qint32>({std::numeric_limits<qint32>::min(), 0, std::numeric_limits<qint32>::max()}), bytes<qint16>({-32768, 0, 32767})},
            {QAudioFormat::Float, bytes<float>({-2.0f, 0.5f, 2.0f}), bytes<qint16>({-32768, 16384, 32767})},
        };

        for (const Case &testCase : cases) {
            AudioPcmConverter converter;
            converter.reset(format(16000, 1, testCase.format));
            const AudioPcmConversion conversion = converter.convert(testCase.input);
            QVERIFY2(conversion.error.isEmpty(), qPrintable(conversion.error));
            QCOMPARE(conversion.pcm16Mono16k, testCase.expected);
        }
    }

    void mixesStereoAndClipsFloatSamples()
    {
        AudioPcmConverter converter;
        converter.reset(format(16000, 2, QAudioFormat::Float));
        const AudioPcmConversion conversion = converter.convert(bytes<float>({2.0f, 2.0f, -2.0f, -2.0f}));

        QVERIFY(conversion.error.isEmpty());
        QCOMPARE(conversion.pcm16Mono16k, bytes<qint16>({32767, -32768}));
        QCOMPARE(conversion.rms, 0.99998474f);
    }

    void preservesResamplingAcrossSplitChunks()
    {
        const QByteArray input = bytes<float>({0.0f, 0.5f, 1.0f, 0.5f, 0.0f});
        AudioPcmConverter whole;
        whole.reset(format(32000, 1, QAudioFormat::Float));
        const QByteArray expected = whole.convert(input).pcm16Mono16k;

        AudioPcmConverter split;
        split.reset(format(32000, 1, QAudioFormat::Float));
        QByteArray actual = split.convert(input.left(2 * int(sizeof(float)))).pcm16Mono16k;
        actual += split.convert(input.mid(2 * int(sizeof(float)))).pcm16Mono16k;

        QCOMPARE(actual, expected);
        QCOMPARE(actual, bytes<qint16>({0, 32767}));
    }

    void preservesPartialFramesAcrossChunks()
    {
        const QByteArray input = bytes<qint16>({1000, -2000, 3000});
        AudioPcmConverter converter;
        converter.reset(format(16000, 1, QAudioFormat::Int16));

        QByteArray actual = converter.convert(input.left(3)).pcm16Mono16k;
        actual += converter.convert(input.mid(3)).pcm16Mono16k;

        QCOMPARE(actual, input);
    }

    void loopbackSilenceKeepsStepWithTheClock()
    {
        LoopbackPcm loopback;
        loopback.useFormat(format(16000, 1, QAudioFormat::Int16));

        // 100 ms with nothing played is 1600 silent samples, owed once.
        QCOMPARE(joined(loopback.silenceUntil(100)), QByteArray(3200, '\0'));
        QVERIFY(loopback.silenceUntil(100).isEmpty());

        // 50 ms of audio covers the clock until 150 ms.
        QByteArray audio;
        for (int pair = 0; pair < 400; ++pair) {
            audio += bytes<qint16>({1000, -1000});
        }
        QCOMPARE(loopback.convert(audio), audio);
        QVERIFY(loopback.silenceUntil(150).isEmpty());
        // Less than 20 ms behind is left to the next fill.
        QVERIFY(loopback.silenceUntil(160).isEmpty());
        QCOMPARE(joined(loopback.silenceUntil(170)), QByteArray(640, '\0'));
    }

    void loopbackFillsGapsBeforePackets()
    {
        LoopbackPcm loopback;
        loopback.useFormat(format(16000, 1, QAudioFormat::Int16));
        const QByteArray tenMs = QByteArray(160, '\1') + QByteArray(160, '\2');

        // Two packets read in one poll, 490 ms apart: the output dropped what
        // played between them, which the stream keeps as silence.
        QCOMPARE(loopback.convert(tenMs), tenMs);
        QCOMPARE(joined(loopback.silenceUntil(500)), QByteArray(490 * 32, '\0'));
        QCOMPARE(loopback.convert(tenMs), tenMs);

        // Audio after the gap is in step again, so none is padded.
        QVERIFY(loopback.silenceUntil(510).isEmpty());
        QCOMPARE(loopback.convert(tenMs), tenMs);
    }

    void loopbackSendsLongSilenceAPollAtATime()
    {
        LoopbackPcm loopback;
        loopback.useFormat(format(16000, 1, QAudioFormat::Int16));

        // 250 ms is two polls' worth and half of one, 4000 samples in all.
        const QList<QByteArray> chunks = loopback.silenceUntil(250);
        QCOMPARE(chunks.size(), 3);
        QCOMPARE(chunks.at(0).size(), 3200);
        QCOMPARE(chunks.at(1).size(), 3200);
        QCOMPARE(chunks.at(2).size(), 1600);
        QCOMPARE(joined(chunks), QByteArray(8000, '\0'));
    }

    void loopbackFillsAStallButNotASleep()
    {
        LoopbackPcm loopback;
        loopback.useFormat(format(16000, 1, QAudioFormat::Int16));
        constexpr qint64 counterStartMs = 1'000'000;

        // Capture stalled 8 s, on both clocks: the packet after it plays 8 s
        // into the timeline, and the 8 s before it are sent as silence.
        QCOMPARE(loopbackPacketMs(8000, counterStartMs + 8000, counterStartMs + 8000), 8000);
        const QList<QByteArray> stall = loopback.silenceUntil(8000);
        QCOMPARE(stall.size(), 80);
        QCOMPARE(joined(stall).size(), 8000 * 32);

        // An hour asleep passes on the performance counter only. The packet
        // that played on waking 100 ms later is 100 ms on, with no hour of
        // silence before it.
        constexpr qint64 hourMs = 60 * 60 * 1000;
        const qint64 wokeCounterMs = counterStartMs + 8000 + hourMs + 100;
        QCOMPARE(loopbackPacketMs(8100, wokeCounterMs, wokeCounterMs), 8100);
        QCOMPARE(joined(loopback.silenceUntil(8100)), QByteArray(3200, '\0'));
        // One that played before the sleep but was read after it is behind
        // the stream, and adds none.
        QVERIFY(loopback.silenceUntil(loopbackPacketMs(8100, wokeCounterMs, counterStartMs + 8050)).isEmpty());
    }

    void loopbackSilentPacketsAreSilenceInEveryFormat()
    {
        // Unsigned 8-bit silence is 128, not 0.
        LoopbackPcm loopback;
        loopback.useFormat(format(16000, 1, QAudioFormat::UInt8));
        QCOMPARE(loopback.convertSilent(160), QByteArray(320, '\0'));

        loopback.useFormat(format(48000, 2, QAudioFormat::Float));
        QCOMPARE(loopback.convertSilent(480 * 8), QByteArray(320, '\0'));
    }

    void loopbackReopenRetriesForAWhileThenGivesUp()
    {
        LoopbackReopen reopen;
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::Failed, 1000));
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::Failed, 10999));
        QVERIFY(!reopen.retries(LoopbackReopen::Refusal::Failed, 11000));

        // A new default output, or the output closing again, gets an allowance of its own.
        reopen.restart();
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::Failed, 12000));
        QVERIFY(!reopen.retries(LoopbackReopen::Refusal::Failed, 22000));
    }

    void loopbackReopenWaitsWhileThereIsNoOutput()
    {
        LoopbackReopen reopen;
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::Failed, 0));
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::NoOutput, 9000));
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::NoOutput, 60000));

        // An output that appears and refuses starts its allowance then.
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::Failed, 61000));
        QVERIFY(reopen.retries(LoopbackReopen::Refusal::Failed, 70000));
        QVERIFY(!reopen.retries(LoopbackReopen::Refusal::Failed, 71000));
    }

    void loopbackConvertsMixFormatAndCarriesOnAcrossOutputs()
    {
        LoopbackPcm loopback;
        loopback.useFormat(format(48000, 2, QAudioFormat::Float));

        // 10 ms of 48 kHz stereo at half scale is 160 samples at 16 kHz.
        QByteArray packet;
        for (int frame = 0; frame < 480; ++frame) {
            packet += bytes<float>({0.5f, 0.5f});
        }
        const QByteArray pcm = loopback.convert(packet);
        QCOMPARE(pcm.size(), 320);
        QCOMPARE(pcm.left(4), bytes<qint16>({16384, 16384}));

        // A new default output's format continues the same stream.
        loopback.useFormat(format(44100, 1, QAudioFormat::Int16));
        QCOMPARE(joined(loopback.silenceUntil(30)), QByteArray(640, '\0'));
    }

    // Voice processing can capture in other layouts than the microphone's.
    void macMicrophoneConvertsWhatItCaptures()
    {
        std::atomic_int handedOver = 0;
        MacMicrophoneInput microphone(
            [&] {
                return std::make_unique<FakeMicrophoneCapture>(
                    format(48000, 2, QAudioFormat::Float), bytes<float>({0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}),
                    &handedOver);
            },
            0);
        QByteArray pcm;
        connect(&microphone, &AudioInput::audioChunk, &microphone, [&](const QByteArray &chunk) { pcm += chunk; });
        QVERIFY(microphone.start());
        QTRY_VERIFY(handedOver >= 10);
        microphone.stop();

        // Three stereo frames at 48 kHz are one sample at 16 kHz.
        QCOMPARE(pcm.size(), handedOver * qsizetype(sizeof(qint16)));
        QCOMPARE(pcm.left(4), bytes<qint16>({16384, 16384}));
    }

    void macMicrophoneDeliversItsPostRollBeforeStopReturns()
    {
        std::atomic_int handedOver = 0;
        MacMicrophoneInput microphone(
            [&] {
                return std::make_unique<FakeMicrophoneCapture>(
                    format(16000, 1, QAudioFormat::Int16), bytes<qint16>({1}), &handedOver);
            },
            200);
        int delivered = 0;
        connect(&microphone, &AudioInput::audioChunk, &microphone, [&] { ++delivered; });
        QVERIFY(microphone.start());
        QVERIFY(microphone.isActive());
        const int beforeStop = handedOver;
        QElapsedTimer stopping;
        stopping.start();
        microphone.stop();

        QVERIFY(stopping.elapsed() >= 200);
        QVERIFY(!microphone.isActive());
        QVERIFY(handedOver > beforeStop);
        QCOMPARE(delivered, handedOver.load());
        QTest::qWait(50);
        QCOMPARE(delivered, handedOver.load());
    }

    // Once, and only for the capture it came from.
    void macMicrophoneReportsAFailureOfItsCapture()
    {
        std::atomic_int handedOver = 0;
        FakeMicrophoneCapture *capture = nullptr;
        MacMicrophoneInput microphone(
            [&] {
                auto created = std::make_unique<FakeMicrophoneCapture>(
                    format(16000, 1, QAudioFormat::Int16), bytes<qint16>({1}), &handedOver);
                capture = created.get();
                return created;
            },
            0);
        QSignalSpy failed(&microphone, &AudioInput::failed);
        QVERIFY(microphone.start());
        capture->fails(QStringLiteral("unplugged"));
        capture->fails(QStringLiteral("unplugged"));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("unplugged"));
        QVERIFY(!microphone.isActive());

        QVERIFY(microphone.start());
        const MacMicrophoneCapture::Fail stale = capture->fails;
        microphone.stop();
        QVERIFY(microphone.start());
        stale(QStringLiteral("stale"));
        QTest::qWait(50);
        QCOMPARE(failed.count(), 1);
        QVERIFY(microphone.isActive());
        microphone.stop();
    }

    void voiceProcessingWarningSaysWhyItCouldNotStart()
    {
        QCOMPARE(echoCancellationNoVoiceProcessingText(-50),
                 QStringLiteral("Echo cancellation is off: macOS could not start voice processing on the microphone "
                                "(error -50). On speakers, the other side may also be written as you; headphones "
                                "avoid it."));
        QCOMPARE(echoCancellationNoVoiceProcessingText(-10875),
                 QStringLiteral("Echo cancellation is off: macOS could not start voice processing on the microphone "
                                "(error -10875). It may need the microphone and the sound output to be on one "
                                "device, such as the Mac's built-in ones. On speakers, the other side may also be "
                                "written as you; headphones avoid it."));
    }
};

} // namespace

int runAudioPcmConverterTests(int argc, char **argv)
{
    AudioPcmConverterTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_audio_pcm_converter.moc"
