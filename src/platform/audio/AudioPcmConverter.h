#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QString>
#include <QVector>

namespace speecher {

struct AudioPcmConversion {
    QByteArray pcm16Mono16k;
    float rms = 0.0f;
    QString error;
};

// The RMS of 16-bit PCM, as a fraction of full scale.
float rmsForPcm16(const QByteArray &pcm);
// Whether audio this loud is speech by Skip silence's threshold, a
// percentage of full scale.
bool isVoiced(float rms, int thresholdPercent);
// Silence as bytes of samples in format.
QByteArray silentPcm(const QAudioFormat &format, qsizetype bytes);

class AudioPcmConverter {
public:
    void reset(const QAudioFormat &sourceFormat);
    AudioPcmConversion convert(const QByteArray &chunk);

private:
    QByteArray encodeOutputSamples(const QVector<float> &samples);

    QAudioFormat m_sourceFormat;
    QByteArray m_pendingInput;
    QVector<float> m_resampleBuffer;
    double m_nextInputPosition = 0.0;
};

} // namespace speecher
