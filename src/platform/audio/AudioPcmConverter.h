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

class AudioPcmConverter {
public:
    void reset(const QAudioFormat &sourceFormat);
    AudioPcmConversion convert(const QByteArray &chunk);

private:
    QByteArray encodeOutputSamples(const QVector<float> &samples);

    QAudioFormat m_sourceFormat;
    QVector<float> m_resampleBuffer;
    double m_nextInputPosition = 0.0;
};

} // namespace speecher
