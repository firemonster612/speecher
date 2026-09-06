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
                      "try\n"
                      "if application id \"%1\" is running then\n"
                      "tell application id \"%1\"\n"
                      "if (player state as string) is \"playing\" then\n"
                      "pause\n"
                      "set end of my pausedPlayers to \"%1\"\n"
                      "end if\n"
                      "end tell\n"
                      "end if\n"
                      "on error\n"
                      "set pauseFailed to true\n"
                      "end try\n")
                      .arg(player);
    }
    source += QStringLiteral(
        "set AppleScript's text item delimiters to linefeed\nreturn my pausedPlayers as text");
    return source;
}

QString resumeScript(const QStringList &players)
{
    QString source = QStringLiteral("set pausedPlayers to {}\n");
    for (const QString &player : players) {
        source += QStringLiteral(
                      "try\n"
                      "if application id \"%1\" is running then\n"
                      "tell application id \"%1\"\n"
                      "if (player state as string) is \"paused\" then play\n"
                      "end tell\n"
                      "end if\n"
                      "on error\n"
                      "set end of my pausedPlayers to \"%1\"\n"
                      "end try\n")
                      .arg(player);
    }
    source += QStringLiteral(
        "set AppleScript's text item delimiters to linefeed\nreturn my pausedPlayers as text");
    return source;
}

} // namespace

MacMediaController::MacMediaController(QObject *parent)
    : MacMediaController(
          [] {
              QStringList players;
              for (const QString &player : mediaPlayerBundleIdentifiers()) {
                  if (isRunning(player)) players << player;
              }
              return players;
          },
          [this](Action action, const QStringList &players, Completion completion) {
              runAppleScript(this, action == Action::Pause ? pauseScript(players) : resumeScript(players),
                             [action, players, completion = std::move(completion)](bool ok, const QString &output) {
                  completion(ok ? output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                : action == Action::Resume ? players : QStringList{});
              });
          }, parent)
{
}

} // namespace speecher
