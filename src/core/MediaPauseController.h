#pragma once

#include <QStringList>

#include "dictation/DictationInterfaces.h"

namespace speecher {

class MediaPauseController : public MediaController {
    Q_OBJECT

public:
    explicit MediaPauseController(QObject *parent = nullptr);

    void pausePlaying() override;
    void resumePaused() override;

private:
    QStringList playingPlayers() const;
    bool runPlayerCommand(const QString &player, const QString &command) const;

    QStringList m_pausedPlayers;
};

} // namespace speecher
