#pragma once

#include "dictation/DictationPorts.h"

#include <QAudioFormat>

#include <windows.h>
#include <mmreg.h>

#include <future>
#include <thread>

namespace speecher {

// The sample layout a WASAPI mix format describes, or an invalid format when
// it is one the converter cannot read.
QAudioFormat audioFormatForWave(const WAVEFORMATEX &wave);

// What the speakers play, captured with WASAPI loopback from the default
// output as 16 kHz mono s16 like the microphone, and moved to the new default
// output when that changes. A thread of its own polls the capture, which
// also notices when nothing plays and fills that stretch with silence.
class WinSystemAudioInput final : public AudioInput {
public:
    explicit WinSystemAudioInput(QObject *parent = nullptr);
    ~WinSystemAudioInput() override;

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

private:
    // Runs on the capture thread, and reports through started whether the
    // capture opened.
    void capture(quint64 generation, std::promise<QString> started);
    // Run on the main thread.
    void fail(quint64 generation, const QString &message);

    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;
    // Bumped by every stop, so audio and failures the capture thread queued
    // before it are dropped rather than reaching the next start.
    quint64 m_generation = 0;
};

} // namespace speecher
