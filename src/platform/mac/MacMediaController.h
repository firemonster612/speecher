#pragma once

#include "dictation/DictationPorts.h"

#include <QStringList>

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
    explicit MacMediaController(QObject *parent = nullptr);
    void pausePlaying() override;
    void resumePaused() override;

private:
    QStringList m_pausedPlayers;
    quint64 m_generation = 0;
};

} // namespace speecher
