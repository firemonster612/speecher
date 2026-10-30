#include "platform/audio/AudioPcmConverter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

namespace speecher {
namespace {

constexpr int kOutputSampleRate = 16000;

// A windowed-sinc low-pass for audio about to be resampled down to 16 kHz:
// interpolation alone folds everything above 8 kHz (sibilants, a monitor's
// whine) back into the speech band. Blackman-windowed, it is flat to about
// 6 kHz and down more than 70 dB from 8 kHz; the taps grow with the rate to
// keep that transition the same width in hertz. Unity gain at DC.
QVector<float> lowPassTaps(int sampleRate)
{
    constexpr double cutoffHz = 7000.0;
    const int half = int(std::lround(24.0 * sampleRate / kOutputSampleRate));
    const int count = 2 * half + 1;
    const double cutoff = cutoffHz / sampleRate;
    QVector<float> taps(count);
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const int offset = i - half;
        const double sinc = offset == 0
            ? 2.0 * cutoff
            : std::sin(2.0 * std::numbers::pi * cutoff * offset) / (std::numbers::pi * offset);
        const double phase = 2.0 * std::numbers::pi * i / (count - 1);
        const double window = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
        taps[i] = float(sinc * window);
        sum += taps[i];
    }
    for (float &tap : taps) {
        tap = float(tap / sum);
    }
    return taps;
}

bool usableFormat(const QAudioFormat &format)
{
    return format.sampleRate() > 0
        && format.channelCount() > 0
        && format.bytesPerFrame() > 0
        && format.sampleFormat() != QAudioFormat::Unknown;
}

qint16 int16FromFloat(float sample)
{
    if (sample <= -1.0f) {
        return std::numeric_limits<qint16>::min();
    }
    if (sample >= 1.0f) {
        return std::numeric_limits<qint16>::max();
    }
    return static_cast<qint16>(std::lrint(sample * std::numeric_limits<qint16>::max()));
}

void appendInt16(QByteArray &buffer, float sample)
{
    const qint16 value = int16FromFloat(std::clamp(sample, -1.0f, 1.0f));
    buffer.append(reinterpret_cast<const char *>(&value), int(sizeof(value)));
}

float sampleAt(const char *data, QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8: {
        const auto value = *reinterpret_cast<const quint8 *>(data);
        return (float(value) - 128.0f) / 128.0f;
    }
    case QAudioFormat::Int16: {
        qint16 value = 0;
        std::memcpy(&value, data, sizeof(value));
        return float(value) / 32768.0f;
    }
    case QAudioFormat::Int32: {
        qint32 value = 0;
        std::memcpy(&value, data, sizeof(value));
        return float(double(value) / 2147483648.0);
    }
    case QAudioFormat::Float: {
        float value = 0.0f;
        std::memcpy(&value, data, sizeof(value));
        return std::clamp(value, -1.0f, 1.0f);
    }
    case QAudioFormat::Unknown:
        return 0.0f;
    default:
        return 0.0f;
    }
}

QVector<float> decodeMonoSamples(const QByteArray &data, const QAudioFormat &format, QString *error)
{
    if (!usableFormat(format)) {
        *error = QStringLiteral("The microphone reported an unsupported audio format.");
        return {};
    }

    const int bytesPerFrame = format.bytesPerFrame();
    const int bytesPerSample = format.bytesPerSample();
    const int channels = format.channelCount();
    const int frameCount = data.size() / bytesPerFrame;
    QVector<float> samples;
    samples.reserve(frameCount);

    for (int frame = 0; frame < frameCount; ++frame) {
        const char *frameData = data.constData() + frame * bytesPerFrame;
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            sum += sampleAt(frameData + channel * bytesPerSample, format.sampleFormat());
        }
        samples.append(float(sum / channels));
    }

    return samples;
}

} // namespace

float rmsForPcm16(const QByteArray &pcm)
{
    const int samples = pcm.size() / int(sizeof(qint16));
    if (samples <= 0) {
        return 0.0f;
    }

    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        qint16 value = 0;
        std::memcpy(&value, pcm.constData() + i * int(sizeof(qint16)), sizeof(value));
        const double normalized = double(value) / 32768.0;
        sum += normalized * normalized;
    }
    return float(std::sqrt(sum / samples));
}

bool isVoiced(float rms, int thresholdPercent)
{
    return rms >= float(thresholdPercent) / 100.0f;
}

QByteArray silentPcm(const QAudioFormat &format, qsizetype bytes)
{
    // Unsigned samples are silent at the middle of their range.
    return QByteArray(bytes, format.sampleFormat() == QAudioFormat::UInt8 ? char(0x80) : '\0');
}

void AudioPcmConverter::reset(const QAudioFormat &sourceFormat)
{
    m_sourceFormat = sourceFormat;
    m_pendingInput.clear();
    m_resampleBuffer.clear();
    m_nextInputPosition = 0.0;
    m_lowPassTaps = sourceFormat.sampleRate() > kOutputSampleRate ? lowPassTaps(sourceFormat.sampleRate())
                                                                  : QVector<float>();
    m_lowPassHistory.clear();
}

AudioPcmConversion AudioPcmConverter::convert(const QByteArray &chunk)
{
    AudioPcmConversion result;
    QByteArray input = m_pendingInput + chunk;
    m_pendingInput.clear();
    const int bytesPerFrame = m_sourceFormat.bytesPerFrame();
    if (bytesPerFrame > 0) {
        const int completeBytes = input.size() - input.size() % bytesPerFrame;
        m_pendingInput = input.mid(completeBytes);
        input.truncate(completeBytes);
    }
    if (m_sourceFormat.sampleRate() == kOutputSampleRate
        && m_sourceFormat.channelCount() == 1
        && m_sourceFormat.sampleFormat() == QAudioFormat::Int16) {
        result.pcm16Mono16k = input;
    } else {
        const QVector<float> samples = decodeMonoSamples(input, m_sourceFormat, &result.error);
        if (!samples.isEmpty()) {
            result.pcm16Mono16k = encodeOutputSamples(samples);
        }
    }
    result.rms = rmsForPcm16(result.pcm16Mono16k);
    return result;
}

QByteArray AudioPcmConverter::encodeOutputSamples(const QVector<float> &samples)
{
    if (m_sourceFormat.sampleRate() == kOutputSampleRate) {
        QByteArray pcm;
        pcm.reserve(samples.size() * int(sizeof(qint16)));
        for (float sample : samples) {
            appendInt16(pcm, sample);
        }
        return pcm;
    }

    m_resampleBuffer += m_lowPassTaps.isEmpty() ? samples : lowPassed(samples);
    if (m_resampleBuffer.size() < 2) {
        return {};
    }

    const double inputStep = double(m_sourceFormat.sampleRate()) / double(kOutputSampleRate);
    QByteArray pcm;
    while (m_nextInputPosition + 1.0 < double(m_resampleBuffer.size())) {
        const int index = int(std::floor(m_nextInputPosition));
        const double fraction = m_nextInputPosition - double(index);
        const float sample = float(double(m_resampleBuffer.at(index)) * (1.0 - fraction)
                                   + double(m_resampleBuffer.at(index + 1)) * fraction);
        appendInt16(pcm, sample);
        m_nextInputPosition += inputStep;
    }

    const int removable = std::min(int(std::floor(m_nextInputPosition)), int(m_resampleBuffer.size()) - 1);
    if (removable > 0) {
        m_resampleBuffer.remove(0, removable);
        m_nextInputPosition -= removable;
    }
    return pcm;
}

// One filtered sample per input sample, each from the taps' width of input
// ending at it, the samples before this chunk carried over in the history.
QVector<float> AudioPcmConverter::lowPassed(const QVector<float> &samples)
{
    const qsizetype count = m_lowPassTaps.size();
    // The stream is taken to have held its first sample before it began, so
    // a level it starts at passes from the first sample instead of rising
    // from silence.
    if (m_lowPassHistory.isEmpty()) {
        m_lowPassHistory.fill(samples.first(), count - 1);
    }
    const QVector<float> input = m_lowPassHistory + samples;
    const float *taps = m_lowPassTaps.constData();
    QVector<float> output(samples.size());
    for (qsizetype i = 0; i < samples.size(); ++i) {
        const float *window = input.constData() + i;
        double sum = 0.0;
        for (qsizetype tap = 0; tap < count; ++tap) {
            sum += double(taps[tap]) * window[tap];
        }
        output[i] = float(sum);
    }
    m_lowPassHistory = input.last(count - 1);
    return output;
}

} // namespace speecher
