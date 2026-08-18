#include "platform/mac/MacMediaController.h"

#include <QDebug>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

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

} // namespace

MacMediaController::MacMediaController(QObject *parent)
    : MediaController(parent)
{
}

void MacMediaController::pausePlaying()
{
    m_pausedPlayers.clear();
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
            m_pausedPlayers << player;
        }
    }

    qInfo() << "media paused players=" << m_pausedPlayers.size();
}

void MacMediaController::resumePaused()
{
    const QStringList players = m_pausedPlayers;
    m_pausedPlayers.clear();
    for (const QString &player : players) {
        tellPlayer(player, QStringLiteral("play"));
    }
    qInfo() << "media resumed players=" << players.size();
}

} // namespace speecher
