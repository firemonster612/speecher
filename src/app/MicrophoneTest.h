#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class ApplicationController;
class AudioInput;

// The Test microphone row's test: a live level from the input device the
// settings name, read through an audio input of its own so no Dictation
// Session starts or is disturbed. The device is open only while running().
// Besides stop(), a Dictation Session starting and a saved change of device
// end the test, and destroying the test ends it too.
class MicrophoneTest final : public QObject {
    Q_OBJECT

public:
    explicit MicrophoneTest(ApplicationController &controller, QObject *parent = nullptr);
    ~MicrophoneTest() override;

    bool running() const { return m_input != nullptr; }
    // Emits failed, after runningChanged(false), when the device will not open.
    void start();
    void stop();

signals:
    // The audio input's level, 0 to 1: the same signal that drives the popup
    // waveform during a Dictation Session.
    void levelChanged(float level);
    void runningChanged(bool running);
    void failed(const QString &message);

private:
    ApplicationController &m_controller;
    AudioInput *m_input = nullptr;
    QString m_deviceId;
};

} // namespace speecher
