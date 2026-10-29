#pragma once

#include <QElapsedTimer>
#include <QObject>

#include <functional>

class QTimer;

namespace speecher {

class SettingsStore;

// Turns the Global Shortcut's presses and releases into dictation toggles,
// starts and stops under the activation mode in settings (toggle,
// push-to-talk or hybrid), and learns along the way whether the bound
// backend reports releases at all.
class ShortcutGesture : public QObject {
    Q_OBJECT

public:
    // sessionActive answers whether a dictation is under way or about to
    // start, which decides whether a press starts one or ends it.
    ShortcutGesture(const SettingsStore &settings, std::function<bool()> sessionActive, QObject *parent = nullptr);

    void press();
    // heldMs is how long the binder saw the key held, or negative when it
    // cannot tell.
    void release(qint64 heldMs);
    // The binding or its backend changed: forget everything learned so far.
    void forget();
    // Drops a push-to-talk start still waiting out the misfire window.
    void dropDeferredStart();
    // The session the held key started is gone; its release must not act.
    void forgetStartedSession();

signals:
    void toggleRequested();
    void startRequested();
    void stopRequested();

private:
    // False once a press has proved that the bound backend never reports key
    // release, which is what makes push-to-talk behave as toggle. Answered from
    // what the shortcut has already done, never probed; it starts out true.
    bool reportsRelease() const;

    const SettingsStore &m_settings;
    std::function<bool()> m_sessionActive;
    QElapsedTimer m_press;
    // Only a press that started a session can end it on release; a press that
    // stopped one must not restart it.
    bool m_startedSession = false;
    bool m_down = false;
    bool m_releaseSeen = false;
    bool m_pressedWhileDown = false;
    QTimer *m_pushToTalkStart = nullptr;
};

} // namespace speecher
