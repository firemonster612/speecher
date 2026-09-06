#pragma once

#include "dictation/DictationPorts.h"

#include <QByteArray>
#include <QString>
#include <QTimer>

namespace speecher {

// End-to-end test seam behind SPEECHER_AUDIO_WAV: replaces microphone capture
// with a 16 kHz mono s16 WAV streamed in real time, so a headless machine with
// no usable input device can still drive the real speech providers. After the
// file ends it keeps producing silence until stop(), like an idle microphone.
class WavFileAudioInput final : public AudioInput {
public:
    explicit WavFileAudioInput(const QString &path, QObject *parent = nullptr);

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

private:
    void emitChunk();

    QString m_path;
    QByteArray m_pcm;
    qsizetype m_offset = 0;
    QTimer m_timer;
};

} // namespace speecher
