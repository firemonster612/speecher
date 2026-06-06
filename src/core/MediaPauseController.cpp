#include "core/MediaPauseController.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>

namespace speecher {

namespace {

constexpr auto mprisPrefix = "org.mpris.MediaPlayer2.";
constexpr auto mprisPath = "/org/mpris/MediaPlayer2";
constexpr auto mprisPlayerInterface = "org.mpris.MediaPlayer2.Player";

QStringList mprisPlayingPlayers()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusConnectionInterface *busInterface = bus.interface();
    if (!busInterface) {
        qInfo() << "media pause skipped session dbus unavailable";
        return {};
    }

    const QDBusReply<QStringList> namesReply = busInterface->registeredServiceNames();
    if (!namesReply.isValid()) {
        qInfo() << "media pause skipped no mpris players";
        return {};
    }

    QStringList players;
    for (const QString &service : namesReply.value()) {
        if (!service.startsWith(QLatin1String(mprisPrefix))) {
            continue;
        }

        QDBusInterface properties(service,
                                  QLatin1String(mprisPath),
                                  QStringLiteral("org.freedesktop.DBus.Properties"),
                                  bus);
        const QDBusReply<QDBusVariant> statusReply = properties.call(QStringLiteral("Get"),
                                                                     QLatin1String(mprisPlayerInterface),
                                                                     QStringLiteral("PlaybackStatus"));
        if (!statusReply.isValid()) {
            qInfo().noquote() << "media pause skipped player=" + service
                              << "error=" + statusReply.error().message();
            continue;
        }

        if (statusReply.value().variant().toString() == QStringLiteral("Playing")) {
            players << service;
        }
    }
    return players;
}

bool runMprisPlayerCommand(const QString &player, const QString &command)
{
    QDBusInterface playerInterface(player,
                                   QLatin1String(mprisPath),
                                   QLatin1String(mprisPlayerInterface),
                                   QDBusConnection::sessionBus());
    if (!playerInterface.isValid()) {
        return false;
    }

    const QDBusMessage reply = playerInterface.call(command);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qInfo().noquote() << "media command failed player=" + player
                          << "command=" + command
                          << "error=" + reply.errorMessage();
        return false;
    }
    return true;
}

} // namespace

MediaPauseController::MediaPauseController(QObject *parent)
    : MediaController(parent)
{
}

QStringList MediaPauseController::playingPlayers() const
{
    return mprisPlayingPlayers();
}

bool MediaPauseController::runPlayerCommand(const QString &player, const QString &command) const
{
    return runMprisPlayerCommand(player, command);
}

void MediaPauseController::pausePlaying()
{
    m_pausedPlayers.clear();
    const QStringList players = playingPlayers();
    for (const QString &player : players) {
        if (runPlayerCommand(player, QStringLiteral("Pause"))) {
            m_pausedPlayers << player;
        }
    }

    qInfo() << "media paused players=" << m_pausedPlayers.size();
}

void MediaPauseController::resumePaused()
{
    const QStringList players = m_pausedPlayers;
    m_pausedPlayers.clear();
    for (const QString &player : players) {
        runPlayerCommand(player, QStringLiteral("Play"));
    }
    qInfo() << "media resumed players=" << players.size();
}

} // namespace speecher
