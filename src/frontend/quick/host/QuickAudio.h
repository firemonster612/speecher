#pragma once

#include "app/MicrophoneTest.h"
#include "dictation/DictationPorts.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

#include <functional>
#include <optional>

namespace speecher {

class ApplicationController;

// The UI's Audio singleton: the microphones the platform sees, kept current
// as they come and go, and the microphone test on the saved Input device. It
// carries data, not wording: the UI words the device choices and the test.
// Whatever shows the test holds it, and the test runs while anything holds it
// and it can: it starts again after a dictation, after a stop that was asked
// for while a new hold arrived, and after its device fails while running. A
// device that will not open is tried again only on a new hold, a microphone
// coming or going, or another device saved.
class QuickAudio final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Audio)
    QML_SINGLETON
    // The microphones present now: [{ id, label, isDefault }]. id is what
    // the audioDevice setting stores; isDefault marks the system's default.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool hasMicrophones READ hasMicrophones NOTIFY devicesChanged)
    // stopped, starting (the device is opening) or running.
    Q_PROPERTY(QString testState READ testState NOTIFY testChanged)
    // Whether a hold can start the test now: not while the device opens, nor
    // while a dictation has the microphone.
    Q_PROPERTY(bool canToggleTest READ canToggleTest NOTIFY testChanged)
    // The test's level, 0 to 1; 0 while stopped.
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    // Whether the test has heard input since it last started, on the device
    // it listens to; it stays true once the test stops.
    Q_PROPERTY(bool heard READ heard NOTIFY testChanged)
    // Why the last test could not open its device or stopped, as core words
    // it; empty once a test starts again.
    Q_PROPERTY(QString testError READ testError NOTIFY testChanged)
    // What holds the test now, for a viewer to see whether it is one, and
    // for the host to let go for a window that hides.
    Q_PROPERTY(QObjectList testHolders READ testHolders NOTIFY testChanged)

public:
    // Lists the microphones: the platform's by default, relisted as the
    // system's inputs change. Any other lister is asked only on refreshDevices.
    using DeviceLister = std::function<QList<AudioInputDeviceInfo>()>;

    explicit QuickAudio(ApplicationController *controller,
                        QObject *parent = nullptr,
                        DeviceLister listDevices = {},
                        MicrophoneTest::InputFactory createInput = {});
    ~QuickAudio() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickAudio *create(QQmlEngine *, QJSEngine *);

    QVariantList devices() const { return m_devices; }
    bool hasMicrophones() const { return !m_devices.isEmpty(); }
    QString testState() const;
    bool canToggleTest() const { return m_test.canToggle(); }
    qreal level() const { return m_level; }
    bool heard() const { return m_test.heard(); }
    QString testError() const { return m_testError; }
    QObjectList testHolders() const { return m_holders; }

    // Lists the microphones again. Hot-plugging does this by itself; the
    // UI's Check again asks when the sound server may have changed its mind.
    Q_INVOKABLE void refreshDevices();
    // The holder wants the test running on the saved Input device, from now
    // until it releases the test or is destroyed. Holding again retries a
    // device that would not open.
    Q_INVOKABLE void holdTest(QObject *holder);
    // Drops the holder's hold; the test stops once nothing holds it.
    Q_INVOKABLE void releaseTest(QObject *holder);

signals:
    void devicesChanged();
    void testChanged();
    void levelChanged();

private:
    void setLevel(qreal level);
    // Starts the test if something holds it and nothing says to wait.
    void keepTestRunning();
    // keepTestRunning once the signal at hand has finished.
    void keepTestRunningSoon();

    ApplicationController *m_controller;
    DeviceLister m_listDevices;
    MicrophoneTest m_test;
    QVariantList m_devices;
    qreal m_level = 0;
    QString m_testError;
    QObjectList m_holders;
    // The test reached Running since it last started.
    bool m_testRan = false;
    // The saved device the test last could not open, not tried again until
    // something the class comment names changes.
    std::optional<QString> m_failedDevice;
};

} // namespace speecher
