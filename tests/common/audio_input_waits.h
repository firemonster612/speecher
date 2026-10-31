#pragma once

// Waits on an AudioInput's start or stop, which finish in the background:
// directly, for tests that drive a real input, or through a Dictation
// Session's pause.

#include "dictation/DictationPorts.h"
#include "dictation/DictationSession.h"

#include <QSignalSpy>
#include <QTest>

namespace speecher::test {

// Starts input and waits for started() or failed(). True once it started;
// otherwise error says why.
inline bool startAndWait(AudioInput &input, QString *error = nullptr, int timeoutMs = 10000)
{
    QSignalSpy started(&input, &AudioInput::started);
    QSignalSpy failed(&input, &AudioInput::failed);
    input.start();
    if (!QTest::qWaitFor([&] { return !started.isEmpty() || !failed.isEmpty(); }, timeoutMs) && error) {
        *error = QStringLiteral("The input neither started nor failed within %1 ms").arg(timeoutMs);
    } else if (!failed.isEmpty() && error) {
        *error = failed.first().at(0).toString();
    }
    return !started.isEmpty();
}

// Stops input and waits for stopped(), its post-roll delivered.
inline bool stopAndWait(AudioInput &input, int timeoutMs = 10000)
{
    QSignalSpy stopped(&input, &AudioInput::stopped);
    input.stop();
    return QTest::qWaitFor([&] { return !stopped.isEmpty(); }, timeoutMs);
}

// Pauses session and waits for it to show Paused, which it does once the
// microphone has stopped.
inline bool pauseAndWait(DictationSession &session, int timeoutMs = 1000)
{
    session.pause();
    return QTest::qWaitFor([&] { return session.state() == DictationState::Paused; }, timeoutMs);
}

} // namespace speecher::test
