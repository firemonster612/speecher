#pragma once

#include <QStringList>

#include "dictation/DictationPorts.h"

namespace speecher {

class MediaPauseController : public MediaController {
    Q_OBJECT

public:
    explicit MediaPauseController(QObject *parent = nullptr);

    void pausePlaying() override;
    void resumePaused() override;

private:
    QStringList m_pausedPlayers;
    quint64 m_generation = 0;
};

} // namespace speecher
