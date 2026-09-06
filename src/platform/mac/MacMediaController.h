#pragma once

#include "dictation/DictationPorts.h"

#include <QStringList>

#include <functional>

namespace speecher {

// Pauses the media players macOS scripts through Apple events, and resumes only
// the ones it actually paused. Best effort throughout: a denied Automation
// prompt or a player that is not running is not worth interrupting dictation.
//
// The Apple events go through asynchronous osascript processes. A player that is
// busy, or an Automation prompt the user has not answered, must not block the
// GUI thread.
class MacMediaController : public MediaController {
    Q_OBJECT

public:
    enum class Action { Pause, Resume };
    // Pause returns newly paused players; Resume returns players still owned
    // because playback failed. A quit player no longer needs resuming.
    using Completion = std::function<void(const QStringList &paused)>;
    using ScriptRunner = std::function<void(Action, const QStringList &, Completion)>;

#ifdef Q_OS_MACOS
    explicit MacMediaController(QObject *parent = nullptr);
#endif
    MacMediaController(std::function<QStringList()> runningPlayers,
                       ScriptRunner runScript, QObject *parent = nullptr);
    void pausePlaying() override;
    void resumePaused() override;

private:
    void applyRequestedState();

    std::function<QStringList()> m_runningPlayers;
    ScriptRunner m_runScript;
    QStringList m_pausedPlayers;
    bool m_pauseRequested = false;
    bool m_scriptRunning = false;
};

} // namespace speecher
