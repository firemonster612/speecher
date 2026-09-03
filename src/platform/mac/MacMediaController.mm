#include "platform/mac/MacMediaController.h"

#include <QDebug>
#include <QProcess>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <functional>

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

void runAppleScript(QObject *owner,
                    const QString &source,
                    std::function<void(bool, const QString &)> completion)
{
    auto *process = new QProcess(owner);
    QObject::connect(process,
                     &QProcess::finished,
                     owner,
                     [process, completion](int exitCode, QProcess::ExitStatus status) {
                         const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                         if (exitCode != 0 || status != QProcess::NormalExit) {
                             qWarning().noquote()
                                 << "media script failed:"
                                 << QString::fromUtf8(process->readAllStandardError()).trimmed();
                         }
                         process->deleteLater();
                         completion(exitCode == 0 && status == QProcess::NormalExit, output);
                     });
    QObject::connect(process, &QProcess::errorOccurred, owner, [process, completion](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        qWarning().noquote() << "media script failed:" << process->errorString();
        process->disconnect();
        process->deleteLater();
        completion(false, {});
    });
    process->start(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), source});
}

QString pauseScript(const QStringList &players)
{
    QString source = QStringLiteral("set pausedPlayers to {}\n");
    for (const QString &player : players) {
        source += QStringLiteral(
                      "tell application id \"%1\"\n"
                      "if (player state as string) is \"playing\" then\n"
                      "pause\n"
                      "set end of my pausedPlayers to \"%1\"\n"
                      "end if\n"
                      "end tell\n")
                      .arg(player);
    }
    source += QStringLiteral(
        "set AppleScript's text item delimiters to linefeed\nreturn my pausedPlayers as text");
    return source;
}

QString resumeScript(const QStringList &players)
{
    QString source;
    for (const QString &player : players) {
        source += QStringLiteral("tell application id \"%1\" to play\n").arg(player);
    }
    return source;
}

} // namespace

MacMediaController::MacMediaController(QObject *parent)
    : MediaController(parent)
{
}

void MacMediaController::pausePlaying()
{
    const quint64 generation = ++m_generation;
    m_pausedPlayers.clear();
    QStringList runningPlayers;
    for (const QString &player : mediaPlayerBundleIdentifiers()) {
        // Scripting a player that is not running would launch it.
        if (isRunning(player)) {
            runningPlayers << player;
        }
    }
    if (runningPlayers.isEmpty()) {
        return;
    }
    runAppleScript(this, pauseScript(runningPlayers), [this, generation](bool ok, const QString &output) {
        const QStringList paused = ok ? output.split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList{};
        if (generation != m_generation) {
            // A short dictation can end before the pause script does; whatever
            // it paused still has to come back.
            if (!paused.isEmpty()) {
                runAppleScript(this, resumeScript(paused), [](bool, const QString &) {});
            }
            return;
        }
        m_pausedPlayers = paused;
        qInfo() << "media paused players=" << m_pausedPlayers.size();
    });
}

void MacMediaController::resumePaused()
{
    const quint64 generation = ++m_generation;
    const QStringList players = m_pausedPlayers;
    if (players.isEmpty()) {
        return;
    }
    runAppleScript(this, resumeScript(players), [this, generation, players](bool, const QString &) {
        if (generation != m_generation) {
            return;
        }
        m_pausedPlayers.clear();
        qInfo() << "media resumed players=" << players.size();
    });
}

} // namespace speecher
