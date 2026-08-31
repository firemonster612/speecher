#pragma once

#include "dictation/DictationPorts.h"

#include <QStringList>

namespace speecher {

// Pauses the media players macOS scripts through Apple events, and resumes only
// the ones it actually paused. Best effort throughout: a denied Automation
// prompt or a player that is not running is not worth interrupting dictation.
//
// The Apple events go out on a worker thread. A player that is busy, or an
// Automation prompt the user has not answered, blocks the script for as long as
// it likes, and dictation must not wait for that.
class MacMediaController : public MediaController {
    Q_OBJECT

public:
    explicit MacMediaController(QObject *parent = nullptr);
    void pausePlaying() override;
    void resumePaused() override;

private:
    // Owned by the GUI thread; the worker only ever hands back a result.
    QStringList m_pausedPlayers;
    // False once dictation has asked for its players back, so a pause that is
    // still in flight knows to undo itself.
    bool m_pauseWanted = false;
};

} // namespace speecher
