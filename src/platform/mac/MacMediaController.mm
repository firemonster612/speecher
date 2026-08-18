#include "platform/mac/MacMediaController.h"

#include <QDebug>
#include <QThread>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <memory>
#include <optional>

namespace speecher {
namespace {

QStringList mediaPlayerBundleIdentifiers()
{
    return {
        QStringLiteral("com.apple.Music"),
        QStringLiteral("com.spotify.client"),
        QStringLiteral("com.apple.TV"),
    };
}

bool isRunning(const QString &bundleIdentifier)
{
    return [NSRunningApplication
               runningApplicationsWithBundleIdentifier:bundleIdentifier.toNSString()]
               .count
        > 0;
}

// Returns the script's result, or nothing when the script failed — which on
// macOS most often means the user declined the Automation prompt.
std::optional<QString> runAppleScript(const QString &source)
{
    @autoreleasepool {
        NSAppleScript *script = [[NSAppleScript alloc] initWithSource:source.toNSString()];
        NSDictionary *errorInfo = nil;
        NSAppleEventDescriptor *result = [script executeAndReturnError:&errorInfo];
        std::optional<QString> value;
        if (errorInfo) {
            qWarning().noquote() << "media script failed:" << QString::fromNSString(errorInfo.description);
        } else {
            value = result.stringValue ? QString::fromNSString(result.stringValue) : QString();
        }
        [script release];
        return value;
    }
}

std::optional<QString> tellPlayer(const QString &bundleIdentifier, const QString &command)
{
    return runAppleScript(
        QStringLiteral("tell application id \"%1\" to %2").arg(bundleIdentifier, command));
}

QStringList pauseRunningPlayers()
{
    QStringList paused;
    for (const QString &player : mediaPlayerBundleIdentifiers()) {
        // Scripting a player that is not running would launch it.
        if (!isRunning(player)) {
            continue;
        }
        const std::optional<QString> state = tellPlayer(player, QStringLiteral("player state as string"));
        if (!state || state->compare(QStringLiteral("playing"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (tellPlayer(player, QStringLiteral("pause"))) {
            paused << player;
        }
    }
    return paused;
}

} // namespace

MacMediaController::MacMediaController(QObject *parent)
    : MediaController(parent)
{
}

void MacMediaController::pausePlaying()
{
    m_pauseWanted = true;
    auto paused = std::make_shared<QStringList>();
    QThread *thread = QThread::create([paused] { *paused = pauseRunningPlayers(); });
    connect(thread, &QThread::finished, this, [this, paused] {
        m_pausedPlayers = *paused;
        // A short dictation can end before the pause script does; whatever it
        // paused still has to come back.
        if (!m_pauseWanted) {
            resumePaused();
            return;
        }
        qInfo() << "media paused players=" << m_pausedPlayers.size();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MacMediaController::resumePaused()
{
    m_pauseWanted = false;
    const QStringList players = m_pausedPlayers;
    if (players.isEmpty()) {
        return;
    }
    QThread *thread = QThread::create([players] {
        for (const QString &player : players) {
            tellPlayer(player, QStringLiteral("play"));
        }
    });
    connect(thread, &QThread::finished, this, [this, players] {
        m_pausedPlayers.clear();
        qInfo() << "media resumed players=" << players.size();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace speecher
