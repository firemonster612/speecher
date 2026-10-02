#pragma once

#include "core/settings/SettingsSchema.h"

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

class ApplicationController;
class AudioInput;

// The Test microphone row's test: a live level from one input device, read
// through an audio input of its own so no Dictation Session starts or is
// disturbed. The device is open only while the test is not Stopped.
// Besides stop(), a Dictation Session starting and a saved change to another
// device end the test, and destroying the test ends it too.
class MicrophoneTest final : public QObject {
    Q_OBJECT

public:
    // Makes the test's audio input: the platform composition's by default.
    using InputFactory = std::function<AudioInput *(QObject *parent)>;

    explicit MicrophoneTest(ApplicationController &controller,
                            QObject *parent = nullptr,
                            InputFactory createInput = {});
    ~MicrophoneTest() override;

    MicrophoneTestState state() const { return m_state; }
    // Whether the row's button may be clicked: not while the device is still
    // opening, and not while a Dictation Session is starting or listening.
    bool canToggle() const;
    // Does nothing unless the test is Stopped and canToggle(). Emits failed,
    // after it is Stopped again, when the device will not open.
    void start(const QString &deviceId);
    // While Starting, takes effect once the device has finished opening.
    void stop();
    // What the row's button does: start() when Stopped, otherwise stop().
    void toggle(const QString &deviceId);

signals:
    // state() or canToggle() moved.
    void changed();
    // The audio input's level, 0 to 1: the same signal that drives the popup
    // waveform during a Dictation Session.
    void levelChanged(float level);
    void failed(const QString &message);

private:
    void setState(MicrophoneTestState state);
    void closeDevice();

    InputFactory m_createInput;
    AudioInput *m_input = nullptr;
    bool m_dictationListening = false;
    QString m_deviceId;
    MicrophoneTestState m_state = MicrophoneTestState::Stopped;
    // A stop that arrived while the input's start() was still waiting for
    // the device: closing it then would free the input under its own call.
    bool m_stopRequested = false;
    // Why the input failed while Starting, reported once start() returns.
    QString m_startFailure;
};

} // namespace speecher
