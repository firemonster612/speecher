#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

class ApplicationController;
class AudioInput;

// Where the test is: Starting while the device opens.
enum class MicrophoneTestState { Stopped, Starting, Running };

// The Test microphone row's test: a live level from one input device, read
// through an audio input of its own so no Dictation Session starts or is
// disturbed. The device is open only while the test is not Stopped.
// Besides stop(), a Dictation Session starting ends the test, and destroying
// the test ends it too. A saved change to another device forgets what was
// heard, ends a test still Starting and, soon after, moves a running test to
// that device.
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
    // Whether this run, or the last one once Stopped, has heard input: the
    // level rose past what a dead or muted microphone gives. A start, the
    // device failing, or another device saved forgets it.
    bool heard() const { return m_heard; }
    // Does nothing unless the test is Stopped and canToggle(). Emits failed,
    // after it is Stopped again, when the device will not open.
    void start(const QString &deviceId);
    // While Starting, takes effect once the device has finished opening.
    void stop();

signals:
    // state(), canToggle() or heard() moved.
    void changed();
    // The audio input's level, 0 to 1: the same signal that drives the popup
    // waveform during a Dictation Session.
    void levelChanged(float level);
    // The device the test opened would not open or stopped working; it may
    // no longer be the saved one.
    void failed(const QString &deviceId, const QString &message);

private:
    void setState(MicrophoneTestState state);
    void closeDevice();

    InputFactory m_createInput;
    AudioInput *m_input = nullptr;
    bool m_dictationListening = false;
    QString m_deviceId;
    MicrophoneTestState m_state = MicrophoneTestState::Stopped;
    bool m_heard = false;
    // A stop that arrived while the input's start() was still waiting for
    // the device: closing it then would free the input under its own call.
    bool m_stopRequested = false;
    // Why the input failed while Starting, reported once start() returns.
    QString m_startFailure;
};

} // namespace speecher
