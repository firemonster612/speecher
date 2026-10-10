#include "common/test_suites.h"

#include "app/PlatformComposition.h"
#include "recording/RecordingPresentation.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace speecher;

namespace {

constexpr int kSampleRate = 16000;
constexpr qsizetype kSecond = kSampleRate;
// Each 10 s, the far end talks for 8 s and then the user alone for most of 2.
// A user who talks over the far end does so from 3 s to 5 s.
constexpr qsizetype kTurn = 10 * kSecond;
constexpr qsizetype kFarEndTalks = 8 * kSecond;
constexpr qsizetype kNearEndStarts = kFarEndTalks + 4 * kSecond / 10;
constexpr qsizetype kNearEndStops = kTurn - 2 * kSecond / 10;
constexpr qsizetype kOverlapStarts = 3 * kSecond;
constexpr qsizetype kOverlapStops = 5 * kSecond;
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

// How a call reaches the canceller.
struct Call {
    int seconds = 60;
    // How fast the clock capturing system audio runs.
    double referencePpm = 0.0;
    // Where system audio starts, against the microphone's first sample.
    qsizetype referenceStart = 0;
    // How much later than captured each system audio chunk arrives.
    qsizetype referenceDelay = 0;
    // System audio chunks that arrive together, all when the last would.
    qsizetype referenceBurst = 1;
    // System audio chunks due in between arrive together at the end.
    qsizetype referenceStallFrom = 0;
    qsizetype referenceStallTo = 0;
    bool userTalksOver = false;
};

struct EchoRun {
    // What the user says, as it reaches the microphone.
    std::vector<qint16> user;
    std::vector<qint16> microphone;
    std::vector<qint16> cancelled;
};

// Plays a call through canceller: the far end, captured as system audio,
// echoes into the microphone beside the user's own speech. Each stream
// arrives in its chunks, in the order they would.
EchoRun runCall(EchoCanceller &canceller, const Call &call)
{
    const qsizetype samples = call.seconds * kSecond;
    const double referenceRate = 1.0 + call.referencePpm / 1e6;
    std::vector<float> farEnd = speechLikeNoise(samples + 2 * kSecond, 1);
    for (qsizetype i = 0; i < qsizetype(farEnd.size()); ++i) {
        farEnd[size_t(i)] *= farEndTalks(i) ? 1.0f : 0.0f;
    }
    const auto played = [&](double at) {
        const auto before = qsizetype(std::floor(at));
        const auto sample = [&](qsizetype i) { return i >= 0 ? double(farEnd[size_t(i)]) : 0.0; };
        return sample(before) + (sample(before + 1) - sample(before)) * (at - double(before));
    };
    const std::vector<float> nearEnd = speechLikeNoise(samples, 2);
    const auto userTalks = [&](qsizetype i) {
        const qsizetype inTurn = i % kTurn;
        return nearEndTalks(i) || (call.userTalksOver && inTurn >= kOverlapStarts && inTurn < kOverlapStops);
    };

    EchoRun run;
    run.user.resize(size_t(samples));
    run.microphone.resize(size_t(samples));
    for (qsizetype i = 0; i < samples; ++i) {
        run.user[size_t(i)] = clamped(userTalks(i) ? nearEnd[size_t(i)] : 0.0);
        run.microphone[size_t(i)] = clamped(0.4 * played(double(i - kEchoDelay))
                                            + 0.15 * played(double(i - kReflectionDelay)) + run.user[size_t(i)]);
    }
    const qsizetype referenceSamples = qsizetype(double(samples + kSecond - call.referenceStart) * referenceRate);
    std::vector<qint16> reference(static_cast<size_t>(referenceSamples));
    for (qsizetype i = 0; i < referenceSamples; ++i) {
        reference[size_t(i)] = clamped(played(double(call.referenceStart) + double(i) / referenceRate));
    }
    const auto referenceArrives = [&](qsizetype chunk) {
        const qsizetype last = (chunk / call.referenceBurst + 1) * call.referenceBurst;
        const double due =
            double(call.referenceStart + call.referenceDelay) + double(last * kReferenceChunk) / referenceRate;
        return due >= double(call.referenceStallFrom) && due < double(call.referenceStallTo)
            ? double(call.referenceStallTo)
            : due;
    };

    QByteArray cancelled;
    qsizetype referenceSent = 0;
    qsizetype microphoneSent = 0;
    while (microphoneSent < samples) {
        const qsizetype chunk = referenceSent / kReferenceChunk;
        if (referenceSent + kReferenceChunk <= referenceSamples
            && referenceArrives(chunk) < double(microphoneSent + kMicrophoneChunk)) {
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

double decibels(double ratio)
{
    return 10.0 * std::log10(ratio);
}

struct Cancellation {
    // How much quieter the far end's echo came out in its worst turn, in dB.
    double farEndDrop = 0.0;
    // How much quieter the user came out when alone, and over the far end.
    double nearEndDrop = 0.0;
    double overlapDrop = 0.0;
};

// Over every turn but the first, which AEC3 spends finding the echo.
Cancellation measure(const EchoRun &run)
{
    const qsizetype end = qsizetype(run.microphone.size());
    Cancellation cancellation{std::numeric_limits<double>::max(), 0.0, 0.0};
    double userIn = 0.0, userOut = 0.0, overlapIn = 0.0, overlapOut = 0.0;
    for (qsizetype turn = kTurn; turn < end; turn += kTurn) {
        // Where the user talks over the far end, only the far end before
        // them counts as echo.
        double echoIn = energy(run.microphone, turn, turn + kFarEndTalks);
        double echoOut = energy(run.cancelled, turn, turn + kFarEndTalks);
        if (energy(run.user, turn + kOverlapStarts, turn + kOverlapStops) > 0.0) {
            echoIn = energy(run.microphone, turn, turn + kOverlapStarts);
            echoOut = energy(run.cancelled, turn, turn + kOverlapStarts);
        }
        cancellation.farEndDrop = std::min(cancellation.farEndDrop, decibels(echoIn / echoOut));
        userIn += energy(run.user, turn + kNearEndStarts, turn + kNearEndStops);
        userOut += energy(run.cancelled, turn + kNearEndStarts, turn + kNearEndStops);
        overlapIn += energy(run.user, turn + kOverlapStarts, turn + kOverlapStops);
        overlapOut += energy(run.cancelled, turn + kOverlapStarts, turn + kOverlapStops);
    }
    cancellation.nearEndDrop = decibels(userIn / userOut);
    if (overlapIn > 0.0) {
        cancellation.overlapDrop = decibels(overlapIn / overlapOut);
    }
    return cancellation;
}

std::unique_ptr<EchoCanceller> createCanceller()
{
    QString warning;
    std::unique_ptr<EchoCanceller> canceller = platformComposition()->createEchoCanceller(&warning);
    if (!canceller) {
        qWarning().noquote() << warning;
    }
    return canceller;
}

} // namespace

Q_DECLARE_METATYPE(Call)

namespace {

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

    void cancelsTheFarEndAndKeepsTheNearEnd_data()
    {
        QTest::addColumn<Call>("call");
        QTest::newRow("together") << Call{};
        QTest::newRow("system audio 150 ms late") << Call{.referenceDelay = 15 * kSecond / 100};
        QTest::newRow("system audio in 300 ms bursts") << Call{.referenceBurst = 3};
        QTest::newRow("system audio starts 0.5 s sooner") << Call{.referenceStart = -kSecond / 2};
        QTest::newRow("system audio starts 0.5 s later") << Call{.referenceStart = kSecond / 2};
        // AEC3 alone follows 300 ppm of drift for about 15 minutes, until the
        // echo moves out of its reach; after that the pairing has to slide.
        QTest::newRow("system audio clock fast") << Call{.seconds = 1000, .referencePpm = 300.0};
        QTest::newRow("system audio clock slow") << Call{.seconds = 1000, .referencePpm = -300.0};
        QTest::newRow("system audio 150 ms late, clock fast")
            << Call{.seconds = 180, .referencePpm = 300.0, .referenceDelay = 15 * kSecond / 100};
        // While the far end is quiet; a misstep shows once it talks again.
        QTest::newRow("system audio stalls 0.9 s")
            << Call{.referenceStallFrom = 286 * kSecond / 10, .referenceStallTo = 295 * kSecond / 10};
    }

    void cancelsTheFarEndAndKeepsTheNearEnd()
    {
#ifndef SPEECHER_WITH_WEBRTC_AEC
        QSKIP("Built without webrtc-audio-processing");
#endif
        QFETCH(Call, call);
        const std::unique_ptr<EchoCanceller> canceller = createCanceller();
        QVERIFY(canceller);

        const EchoRun run = runCall(*canceller, call);

        QCOMPARE(run.cancelled.size(), run.microphone.size());
        const Cancellation cancellation = measure(run);
        qInfo("far end at least %.1f dB quieter each turn, near end %.1f dB quieter", cancellation.farEndDrop,
              cancellation.nearEndDrop);
        QVERIFY2(cancellation.farEndDrop > 15.0, qPrintable(QString::number(cancellation.farEndDrop)));
        QVERIFY2(cancellation.nearEndDrop < 3.0, qPrintable(QString::number(cancellation.nearEndDrop)));
    }

    void keepsTheUserTalkingOverTheFarEnd()
    {
#ifndef SPEECHER_WITH_WEBRTC_AEC
        QSKIP("Built without webrtc-audio-processing");
#endif
        const std::unique_ptr<EchoCanceller> canceller = createCanceller();
        QVERIFY(canceller);

        const EchoRun run = runCall(*canceller, Call{.userTalksOver = true});

        // AEC3 turns the user down about 5 dB while the far end talks too; at
        // 6 dB they would come out at half their amplitude.
        const Cancellation cancellation = measure(run);
        qInfo("user over the far end %.1f dB quieter", cancellation.overlapDrop);
        QVERIFY2(cancellation.overlapDrop < 6.0, qPrintable(QString::number(cancellation.overlapDrop)));
    }

    void returnsTheMicrophoneToItsLastSample()
    {
#ifndef SPEECHER_WITH_WEBRTC_AEC
        QSKIP("Built without webrtc-audio-processing");
#endif
        const std::unique_ptr<EchoCanceller> canceller = createCanceller();
        QVERIFY(canceller);
        // A second of quiet, then 5 ms of a 1 kHz tone.
        const qsizetype toneSamples = 5 * kSecond / 1000;
        std::vector<qint16> microphone(size_t(kSecond + toneSamples), 0);
        for (qsizetype i = 0; i < toneSamples; ++i) {
            microphone[size_t(kSecond + i)] =
                clamped(8000.0 * std::sin(2.0 * M_PI * 1000.0 * double(i) / kSampleRate));
        }

        QByteArray cancelled;
        for (qsizetype sent = 0; sent < qsizetype(microphone.size()); sent += kMicrophoneChunk) {
            canceller->addReference(QByteArray(kMicrophoneChunk * qsizetype(sizeof(qint16)), '\0'));
            cancelled += canceller->process(
                pcm(microphone, sent, std::min(kMicrophoneChunk, qsizetype(microphone.size()) - sent)));
        }
        cancelled += canceller->flush();

        QCOMPARE(cancelled.size(), qsizetype(microphone.size() * sizeof(qint16)));
        std::vector<qint16> out(microphone.size());
        std::memcpy(out.data(), cancelled.constData(), size_t(cancelled.size()));
        const double kept =
            energy(out, kSecond, kSecond + toneSamples) / energy(microphone, kSecond, kSecond + toneSamples);
        qInfo("%.2f of the last 5 ms came back", kept);
        QVERIFY2(kept > 0.5, qPrintable(QString::number(kept)));
    }
};

} // namespace

int runEchoCancellerTests(int argc, char **argv)
{
    EchoCancellerTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_echo_canceller.moc"
