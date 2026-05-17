#pragma once

#include <QObject>
#include <QStringList>

namespace speecher {

class MediaPauseController : public QObject {
    Q_OBJECT

public:
    explicit MediaPauseController(QObject *parent = nullptr);

    void pausePlaying();
    void resumePaused();

private:
    QStringList playingPlayers() const;
    bool runPlayerCommand(const QString &player, const QString &command) const;

    QStringList m_pausedPlayers;
};

} // namespace speecher
