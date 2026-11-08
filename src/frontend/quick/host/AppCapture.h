#pragma once

#include "core/Target.h"

#include <QObject>
#include <QTimer>

#include <functional>

namespace speecher {

class TargetProvider;

// How long each part of an AppCapture takes; tests shorten them.
struct AppCaptureTiming {
    // One countdown second.
    int tickMs = 1000;
    // How many ticks past the countdown it waits for another app.
    int waitTicks = 10;
    // How long that app's window settles before it is captured.
    int settleMs = 700;
};

// Switch to it and Test's capture of another app: counts down, waits for
// another app to come to the front, lets its window settle, then captures it
// through the TargetProvider as dictation does. What to do with the Target is
// the caller's.
class AppCapture final : public QObject {
    Q_OBJECT

public:
    // provider may be null where nothing can tell which app is in front, and
    // then every capture fails. busy says a dictation is under way, whose
    // capture this one would drop; inFront that Speecher is still the app in
    // front.
    AppCapture(TargetProvider *provider, std::function<bool()> busy, std::function<bool()> inFront,
               AppCaptureTiming timing = {}, QObject *parent = nullptr);

    // From start until it captures, fails or is cancelled.
    bool waiting() const { return m_waiting; }
    // The countdown's seconds left; at 0 and below while it waits for
    // another app.
    int countdown() const { return m_countdown; }

    // Starts over: counts down seconds, then waits.
    void start(int seconds);
    // Stops waiting, answering nothing; a capture already asked of the
    // provider is dropped.
    void cancel();

signals:
    // waiting or the countdown changed.
    void changed();
    void captured(const Target &target);
    // No other app came to the front in time, the provider can't capture,
    // a dictation is under way, or the capture was cancelled while the
    // window settled.
    void failed();

private:
    void tick();
    void captureFront();
    // Stops waiting and says changed.
    void finish();

    TargetProvider *m_provider;
    std::function<bool()> m_busy;
    std::function<bool()> m_inFront;
    AppCaptureTiming m_timing;
    QTimer m_tick;
    bool m_waiting = false;
    int m_countdown = 0;
};

} // namespace speecher
