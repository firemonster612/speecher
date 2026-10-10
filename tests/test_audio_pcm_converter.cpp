#include "common/test_suites.h"
#include "platform/audio/AudioPcmConverter.h"
#include "platform/audio/LoopbackPcm.h"

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

QAudioFormat format(int sampleRate, int channels, QAudioFormat::SampleFormat sampleFormat)
{
    QAudioFormat result;
    result.setSampleRate(sampleRate);
    result.setChannelCount(channels);
    result.setSampleFormat(sampleFormat);
    return result;
}

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
        QCOMPARE(loopback.silenceUntil(100), QByteArray(3200, '\0'));
        QVERIFY(loopback.silenceUntil(100).isEmpty());

        // 50 ms of audio covers the clock until 150 ms.
        QByteArray audio;
        for (int pair = 0; pair < 400; ++pair) {
            audio += bytes<qint16>({1000, -1000});
        }
        QCOMPARE(loopback.convert(audio), audio);
        QVERIFY(loopback.silenceUntil(150).isEmpty());
        QCOMPARE(loopback.silenceUntil(170), QByteArray(640, '\0'));
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
        QCOMPARE(loopback.silenceUntil(20), QByteArray(320, '\0'));
    }
};

} // namespace

int runAudioPcmConverterTests(int argc, char **argv)
{
    AudioPcmConverterTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_audio_pcm_converter.moc"
