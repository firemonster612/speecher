#include "common/test_suites.h"

#include "app/PlatformComposition.h"
#include "recording/RecordingPresentation.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace speecher;

namespace {

constexpr int kSampleRate = 16000;
constexpr qsizetype kSecond = kSampleRate;
// Each 10 s, the far end talks for 8 s and then the user alone for most of 2.
constexpr qsizetype kTurn = 10 * kSecond;
constexpr qsizetype kFarEndTalks = 8 * kSecond;
constexpr qsizetype kNearEndStarts = kFarEndTalks + 4 * kSecond / 10;
constexpr qsizetype kNearEndStops = kTurn - 2 * kSecond / 10;
// The speakers reach the microphone 20 ms after system audio has them, at
// 0.4, with a reflection 5 ms later.
constexpr qsizetype kEchoDelay = 320;
constexpr qsizetype kReflectionDelay = 400;
// As the inputs deliver them: system audio 100 ms at a time, the microphone 50.
constexpr qsizetype kReferenceChunk = kSecond / 10;
constexpr qsizetype kMicrophoneChunk = kSecond / 20;

// White noise below about 2 kHz in syllables of 200 ms, at -20 dBFS.
std::vector<float> speechLikeNoise(qsizetype samples, quint32 seed)
{
    QRandomGenerator random(seed);
    std::vector<float> signal(size_t(samples), 0.0f);
    float low = 0.0f;
    float lower = 0.0f;
    double energy = 0.0;
    for (qsizetype i = 0; i < samples; ++i) {
        low += 0.5f * (float(random.generateDouble() * 2.0 - 1.0) - low);
        lower += 0.5f * (low - lower);
        signal[size_t(i)] = lower * float(std::abs(std::sin(M_PI * 2.5 * double(i) / kSampleRate)));
        energy += double(signal[size_t(i)]) * signal[size_t(i)];
    }
    const float gain = float(3277.0 / std::sqrt(energy / double(samples)));
    for (float &sample : signal) {
        sample *= gain;
    }
    return signal;
}

bool farEndTalks(qsizetype sample)
{
    return sample >= 0 && sample % kTurn < kFarEndTalks;
}

bool nearEndTalks(qsizetype sample)
{
    return sample % kTurn >= kNearEndStarts && sample % kTurn < kNearEndStops;
}

QByteArray pcm(const std::vector<qint16> &samples, qsizetype from, qsizetype count)
{
    return QByteArray(reinterpret_cast<const char *>(samples.data() + from), count * qsizetype(sizeof(qint16)));
}

qint16 clamped(double sample)
{
    return qint16(std::clamp(std::lround(sample), -32768L, 32767L));
}

struct EchoRun {
    std::vector<qint16> microphone;
    std::vector<qint16> cancelled;
};

// Plays seconds of a call through canceller: the far end, captured as system
// audio by a clock referencePpm fast, echoes into the microphone beside the
// user's own speech. Each stream arrives in its chunks, in the order they
// would.
EchoRun runCall(EchoCanceller &canceller, int seconds, double referencePpm)
{
    const qsizetype samples = seconds * kSecond;
    const double referenceRate = 1.0 + referencePpm / 1e6;
    std::vector<float> farEnd = speechLikeNoise(samples + kSecond, 1);
    for (qsizetype i = 0; i < qsizetype(farEnd.size()); ++i) {
        farEnd[size_t(i)] *= farEndTalks(i) ? 1.0f : 0.0f;
    }
    const auto played = [&](qsizetype i) { return i >= 0 ? double(farEnd[size_t(i)]) : 0.0; };
    const std::vector<float> nearEnd = speechLikeNoise(samples, 2);

    EchoRun run;
    run.microphone.resize(size_t(samples));
    for (qsizetype i = 0; i < samples; ++i) {
        const double user = nearEndTalks(i) ? nearEnd[size_t(i)] : 0.0;
        run.microphone[size_t(i)] = clamped(0.4 * played(i - kEchoDelay) + 0.15 * played(i - kReflectionDelay) + user);
    }
    const qsizetype referenceSamples = qsizetype(double(samples) * referenceRate);
    std::vector<qint16> reference(static_cast<size_t>(referenceSamples));
    for (qsizetype i = 0; i < referenceSamples; ++i) {
        const double at = double(i) / referenceRate;
        const auto before = qsizetype(at);
        const double after = at - double(before);
        reference[size_t(i)] = clamped(played(before) * (1.0 - after) + played(before + 1) * after);
    }

    QByteArray cancelled;
    qsizetype referenceSent = 0;
    qsizetype microphoneSent = 0;
    while (microphoneSent < samples) {
        const double referenceArrives = double(referenceSent + kReferenceChunk) / (kSampleRate * referenceRate);
        const double microphoneArrives = double(microphoneSent + kMicrophoneChunk) / kSampleRate;
        if (referenceSent + kReferenceChunk <= referenceSamples && referenceArrives <= microphoneArrives) {
            canceller.addReference(pcm(reference, referenceSent, kReferenceChunk));
            referenceSent += kReferenceChunk;
        } else {
            cancelled += canceller.process(pcm(run.microphone, microphoneSent, kMicrophoneChunk));
            microphoneSent += kMicrophoneChunk;
        }
    }
    cancelled += canceller.flush();
    run.cancelled.resize(size_t(cancelled.size()) / sizeof(qint16));
    std::memcpy(run.cancelled.data(), cancelled.constData(), size_t(cancelled.size()));
    return run;
}

double energy(const std::vector<qint16> &signal, qsizetype from, qsizetype to)
{
    double sum = 0.0;
    for (qsizetype i = from; i < to; ++i) {
        sum += double(signal[size_t(i)]) * signal[size_t(i)];
    }
    return sum;
}

struct Cancellation {
    // How much quieter the far end's echo came out, in dB.
    double farEndDrop = 0.0;
    // How much quieter the user came out, in dB.
    double nearEndDrop = 0.0;
};

// Over the turns of the last measuredSeconds of run.
Cancellation measure(const EchoRun &run, int measuredSeconds)
{
    const qsizetype end = qsizetype(run.microphone.size());
    double echoIn = 0.0, echoOut = 0.0, userIn = 0.0, userOut = 0.0;
    for (qsizetype turn = end - measuredSeconds * kSecond; turn < end; turn += kTurn) {
        echoIn += energy(run.microphone, turn, turn + kFarEndTalks);
        echoOut += energy(run.cancelled, turn, turn + kFarEndTalks);
        userIn += energy(run.microphone, turn + kNearEndStarts, turn + kNearEndStops);
        userOut += energy(run.cancelled, turn + kNearEndStarts, turn + kNearEndStops);
    }
    return {10.0 * std::log10(echoIn / echoOut), 10.0 * std::log10(userIn / userOut)};
}

class EchoCancellerTests : public QObject {
    Q_OBJECT

private slots:
    void saysWhyALinuxBuildWithoutWebRtcCannotCancel()
    {
#if !defined(Q_OS_LINUX) || defined(SPEECHER_WITH_WEBRTC_AEC)
        QSKIP("Only a Linux build without webrtc-audio-processing has no echo cancellation");
#else
        QString warning;
        QVERIFY(!platformComposition()->createEchoCanceller(&warning));
        QCOMPARE(warning, echoCancellationNotBuiltText());
#endif
    }

    void cancelsTheFarEndAndKeepsTheNearEnd()
    {
#ifndef SPEECHER_WITH_WEBRTC_AEC
        QSKIP("Built without webrtc-audio-processing");
#endif
        QString warning;
        const std::unique_ptr<EchoCanceller> canceller = platformComposition()->createEchoCanceller(&warning);
        QVERIFY2(canceller, qPrintable(warning));

        const EchoRun run = runCall(*canceller, 60, 0.0);

        QCOMPARE(run.cancelled.size(), run.microphone.size());
        const Cancellation cancellation = measure(run, 30);
        qInfo("far end %.1f dB quieter, near end %.1f dB quieter", cancellation.farEndDrop, cancellation.nearEndDrop);
        QVERIFY2(cancellation.farEndDrop > 20.0, qPrintable(QString::number(cancellation.farEndDrop)));
        QVERIFY2(cancellation.nearEndDrop < 3.0, qPrintable(QString::number(cancellation.nearEndDrop)));
    }

    void keepsCancellingAsTheClocksDrift_data()
    {
        QTest::addColumn<double>("referencePpm");
        QTest::newRow("system audio fast") << 100.0;
        QTest::newRow("system audio slow") << -100.0;
    }

    void keepsCancellingAsTheClocksDrift()
    {
#ifndef SPEECHER_WITH_WEBRTC_AEC
        QSKIP("Built without webrtc-audio-processing");
#endif
        QFETCH(double, referencePpm);
        QString warning;
        const std::unique_ptr<EchoCanceller> canceller = platformComposition()->createEchoCanceller(&warning);
        QVERIFY2(canceller, qPrintable(warning));

        const EchoRun run = runCall(*canceller, 300, referencePpm);

        const Cancellation cancellation = measure(run, 30);
        qInfo("far end %.1f dB quieter, near end %.1f dB quieter", cancellation.farEndDrop, cancellation.nearEndDrop);
        QVERIFY2(cancellation.farEndDrop > 20.0, qPrintable(QString::number(cancellation.farEndDrop)));
        QVERIFY2(cancellation.nearEndDrop < 3.0, qPrintable(QString::number(cancellation.nearEndDrop)));
    }
};

} // namespace

int runEchoCancellerTests(int argc, char **argv)
{
    EchoCancellerTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_echo_canceller.moc"
