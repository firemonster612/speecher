#include "core/MediaPauseController.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>

namespace speecher {

namespace {

constexpr auto mprisPrefix = "org.mpris.MediaPlayer2.";
constexpr auto mprisPath = "/org/mpris/MediaPlayer2";
constexpr auto mprisPlayerInterface = "org.mpris.MediaPlayer2.Player";

QDBusMessage playerCommand(const QString &player, const QString &command)
{
    return QDBusMessage::createMethodCall(
        player, QLatin1String(mprisPath), QLatin1String(mprisPlayerInterface), command);
}

QDBusMessage playbackStatusRequest(const QString &player)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        player,
        QLatin1String(mprisPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    message.setArguments({QLatin1String(mprisPlayerInterface), QStringLiteral("PlaybackStatus")});
    return message;
}

} // namespace

MediaPauseController::MediaPauseController(QObject *parent)
    : MediaController(parent)
{
}

void MediaPauseController::pausePlaying()
{
    const quint64 generation = ++m_generation;
    m_pauseRequested = true;
    m_pausedPlayers.clear();
    QDBusMessage listNames = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("ListNames"));
    auto *namesWatcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(listNames, 300), this);
    connect(namesWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, namesWatcher, generation] {
        const QDBusPendingReply<QStringList> names = *namesWatcher;
        namesWatcher->deleteLater();
        if (generation != m_generation || names.isError()) {
            return;
        }
        for (const QString &player : names.value()) {
            if (!player.startsWith(QLatin1String(mprisPrefix))) continue;
            auto *statusWatcher = new QDBusPendingCallWatcher(
                QDBusConnection::sessionBus().asyncCall(playbackStatusRequest(player), 300),
                this);
            connect(statusWatcher, &QDBusPendingCallWatcher::finished, this,
                    [this, statusWatcher, player, generation] {
                const QDBusPendingReply<QDBusVariant> status = *statusWatcher;
                statusWatcher->deleteLater();
                if (generation != m_generation || status.isError()
                    || status.value().variant().toString() != QStringLiteral("Playing")) {
                    return;
                }
                auto *pauseWatcher = new QDBusPendingCallWatcher(
                    QDBusConnection::sessionBus().asyncCall(
                        playerCommand(player, QStringLiteral("Pause")), 300),
                    this);
                connect(pauseWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, pauseWatcher, player, generation] {
                    const QDBusPendingReply<> paused = *pauseWatcher;
                    pauseWatcher->deleteLater();
                    if (paused.isError()) return;
                    if (m_pauseRequested) {
                        if (!m_pausedPlayers.contains(player)) {
                            m_pausedPlayers << player;
                        }
                    } else {
                        QDBusConnection::sessionBus().asyncCall(
                            playerCommand(player, QStringLiteral("Play")), 300);
                    }
                });
            });
        }
    });
}

void MediaPauseController::resumePaused()
{
    ++m_generation;
    m_pauseRequested = false;
    const QStringList players = m_pausedPlayers;
    m_pausedPlayers.clear();
    for (const QString &player : players) {
        QDBusConnection::sessionBus().asyncCall(
            playerCommand(player, QStringLiteral("Play")), 300);
    }
    qInfo() << "media resumed players=" << players.size();
}

} // namespace speecher
