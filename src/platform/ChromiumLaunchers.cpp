#include "platform/ChromiumLaunchers.h"

#include "platform/LinuxDesktopIntegration.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace speecher {
namespace {

const QString accessibilityFlag = QStringLiteral("--force-renderer-accessibility");
// The line that marks a copy as Speecher's. Its value is the SHA-256 of the
// rest of the file, so removal can tell a copy no one has changed.
constexpr char markKey[] = "X-Speecher-Accessibility=";
constexpr char desktopEntryGroup[] = "[Desktop Entry]";

// Flatpaks run inside their sandbox, out of reach of the check for
// Chromium's files, so these are known by id.
const QSet<QString> &chromiumFlatpaks()
{
    static const QSet<QString> ids{
        QStringLiteral("com.google.Chrome"),
        QStringLiteral("org.chromium.Chromium"),
        QStringLiteral("com.brave.Browser"),
        QStringLiteral("com.microsoft.Edge"),
        QStringLiteral("com.visualstudio.code"),
        QStringLiteral("com.slack.Slack"),
        QStringLiteral("com.discordapp.Discord"),
        QStringLiteral("md.obsidian.Obsidian"),
    };
    return ids;
}

// Fedora's and Debian's Chromium start from a shell script in /usr/bin, away
// from Chromium's files.
const QSet<QString> &chromiumWrappers()
{
    static const QSet<QString> names{QStringLiteral("chromium"), QStringLiteral("chromium-browser")};
    return names;
}

struct ExecArgument {
    qsizetype start;
    QString text;
};

// The Exec line's arguments, unquoted, with where each starts in the line.
// Arguments are split on spaces outside double quotes, in which a backslash
// escapes the next character, as the desktop entry specification has it.
QList<ExecArgument> execArguments(const QString &execLine)
{
    QList<ExecArgument> arguments;
    bool quoted = false;
    bool inArgument = false;
    for (qsizetype i = 0; i < execLine.size(); ++i) {
        const QChar c = execLine.at(i);
        if (c == QLatin1Char(' ') && !quoted) {
            inArgument = false;
            continue;
        }
        if (!inArgument) {
            arguments.append(ExecArgument{i, {}});
            inArgument = true;
        }
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (c == QLatin1Char('\\') && quoted && i + 1 < execLine.size()) {
            arguments.last().text.append(execLine.at(++i));
        } else {
            arguments.last().text.append(c);
        }
    }
    return arguments;
}

// A field code such as %U, or Flatpak's file-forwarding marks around one.
// %% is a literal percent sign.
bool isFieldCode(const QString &argument)
{
    return (argument.size() == 2 && argument.startsWith(QLatin1Char('%')) && argument != QLatin1String("%%"))
        || argument.startsWith(QStringLiteral("@@"));
}

// What an Exec line starts, as indexes into its arguments, or -1: the
// program, past an env prefix's options and assignments, and when that
// program is flatpak, the app id it runs. Also the directory env -C starts
// the program in and the PATH env sets for it, empty where it sets none.
struct Launch {
    qsizetype program = -1;
    qsizetype flatpakApp = -1;
    QString directory;
    QStringList searchPath;
};

Launch launchOf(const QList<ExecArgument> &arguments)
{
    const auto named = [](const ExecArgument &argument, QLatin1String name) {
        return QFileInfo(argument.text).fileName() == name;
    };
    Launch launch;
    qsizetype i = 0;
    if (!arguments.isEmpty() && named(arguments.first(), QLatin1String("env"))) {
        bool options = true;
        for (++i; i < arguments.size(); ++i) {
            const QString &argument = arguments.at(i).text;
            if (options && argument == QLatin1String("--")) {
                options = false;
            } else if (options && (argument == QLatin1String("-C") || argument == QLatin1String("--chdir"))) {
                launch.directory = arguments.value(++i).text;
            } else if (options && (argument == QLatin1String("-u") || argument == QLatin1String("--unset"))) {
                ++i;
            } else if (argument.startsWith(QLatin1String("PATH="))) {
                launch.searchPath = argument.mid(5).split(QLatin1Char(':'), Qt::SkipEmptyParts);
            } else if (!(options && argument.startsWith(QLatin1Char('-')))
                       && !argument.contains(QLatin1Char('='))) {
                break;
            }
        }
    }
    if (i >= arguments.size()) {
        return launch;
    }
    launch.program = i;
    if (named(arguments.at(i), QLatin1String("flatpak"))) {
        const auto run = std::find_if(arguments.cbegin() + i, arguments.cend(),
                                      [](const ExecArgument &a) { return a.text == QLatin1String("run"); });
        const auto appId = std::find_if(run, arguments.cend(), [](const ExecArgument &a) {
            return a.text != QLatin1String("run") && !a.text.startsWith(QLatin1Char('-'));
        });
        if (appId != arguments.cend()) {
            launch.flatpakApp = appId - arguments.cbegin();
        }
    }
    return launch;
}

bool holdsChromium(const QDir &directory)
{
    return directory.exists(QStringLiteral("chrome_100_percent.pak"))
        || directory.exists(QStringLiteral("resources.pak"))
        || directory.exists(QStringLiteral("resources/app.asar"));
}

QByteArray checksum(const QByteArray &contents)
{
    return QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
}

// The entry with withAccessibilityFlag() on each Exec line that starts a
// Chromium or Electron app, the main one's or an action's, and Speecher's mark
// under its group header; or nothing when no line starts one or it has no
// such header.
QByteArray accessibleCopy(const QByteArray &entry)
{
    QList<QByteArray> lines = entry.split('\n');
    const qsizetype header = lines.indexOf(desktopEntryGroup);
    if (header < 0) {
        return {};
    }
    bool startsChromium = false;
    for (QByteArray &line : lines) {
        if (!line.startsWith("Exec=")) {
            continue;
        }
        const QString execLine = QString::fromUtf8(line.mid(5));
        if (launchesChromium(execLine)) {
            startsChromium = true;
            line = "Exec=" + withAccessibilityFlag(execLine).toUtf8();
        }
    }
    if (!startsChromium) {
        return {};
    }
    const QByteArray copy = lines.join('\n');
    lines.insert(header + 1, markKey + checksum(copy));
    return lines.join('\n');
}

enum class Owner { Someone, Speecher, ChangedSinceSpeecher };

Owner ownerOf(const QByteArray &entry)
{
    QList<QByteArray> lines = entry.split('\n');
    const auto mark = std::find_if(lines.cbegin(), lines.cend(),
                                   [](const QByteArray &line) { return line.startsWith(markKey); });
    if (mark == lines.cend()) {
        return Owner::Someone;
    }
    const QByteArray recorded = mark->mid(sizeof(markKey) - 1);
    lines.erase(mark);
    return checksum(lines.join('\n')) == recorded ? Owner::Speecher : Owner::ChangedSinceSpeecher;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Whether something, a file or a symlink, even one to nothing, is at path.
bool occupied(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

// Whose the desktop entry at path is. A symlink is the user's, wherever it
// points, so Speecher never writes through one or deletes it.
Owner ownerOfFile(const QString &path)
{
    return QFileInfo(path).isSymLink() ? Owner::Someone : ownerOf(readFile(path));
}

// Writes copy to target, unless target is someone else's, including a file
// that appears there while this runs. False, with error, when the write fails.
bool writeCopy(const QString &target, const QByteArray &copy, QString *error)
{
    const auto fail = [&](const QString &reason) {
        if (error) {
            *error = QStringLiteral("Could not write %1: %2").arg(target, reason);
        }
        return false;
    };
    if (!occupied(target)) {
        // NewOnly refuses, rather than replaces, a file made since the check.
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            return occupied(target) || fail(file.errorString());
        }
        if (file.write(copy) != copy.size() || !file.flush()) {
            const QString reason = file.errorString();
            file.remove();
            return fail(reason);
        }
        return true;
    }
    if (ownerOfFile(target) != Owner::Speecher) {
        return true;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(copy) != copy.size()) {
        return fail(file.errorString());
    }
    // Someone may have edited the copy while this one was written.
    if (ownerOfFile(target) != Owner::Speecher) {
        file.cancelWriting();
        return true;
    }
    return file.commit() || fail(file.errorString());
}

QStringList desktopEntries(const QDir &directory)
{
    return directory.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
}

} // namespace

bool launchesChromium(const QString &execLine)
{
    const QList<ExecArgument> arguments = execArguments(execLine);
    const Launch launch = launchOf(arguments);
    if (launch.program < 0) {
        return false;
    }
    if (launch.flatpakApp >= 0) {
        return chromiumFlatpaks().contains(arguments.at(launch.flatpakApp).text);
    }
    const QString program = arguments.at(launch.program).text;
    if (chromiumWrappers().contains(QFileInfo(program).fileName())) {
        return true;
    }
    // As exec has it, a program named with a slash is a path, here from the
    // directory env -C gives, and one without is looked up in PATH.
    const QString path = program.contains(QLatin1Char('/'))
        ? QDir(launch.directory).absoluteFilePath(program)
        : QStandardPaths::findExecutable(program, launch.searchPath);
    const QString resolved = QFileInfo(path).canonicalFilePath();
    return !resolved.isEmpty() && holdsChromium(QFileInfo(resolved).absoluteDir());
}

QString withAccessibilityFlag(const QString &execLine)
{
    const QList<ExecArgument> arguments = execArguments(execLine);
    if (std::any_of(arguments.cbegin(), arguments.cend(),
                    [](const ExecArgument &a) { return a.text == accessibilityFlag; })) {
        return execLine;
    }
    // Past what the line starts, so the flag reaches the app rather than env
    // or flatpak, and before -- or a field code, after which come files.
    const Launch launch = launchOf(arguments);
    const auto end = std::find_if(arguments.cbegin() + std::max(launch.program, launch.flatpakApp) + 1,
                                  arguments.cend(), [](const ExecArgument &a) {
                                      return a.text == QLatin1String("--") || isFieldCode(a.text);
                                  });
    if (end == arguments.cend()) {
        return execLine + QLatin1Char(' ') + accessibilityFlag;
    }
    return QString(execLine).insert(end->start, accessibilityFlag + QLatin1Char(' '));
}

bool writeAccessibleLaunchers(const QStringList &dataDirs, QString *error)
{
    if (dataDirs.isEmpty()) {
        return true;
    }
    const QDir userApplications(dataDirs.first() + QStringLiteral("/applications"));
    // Deletes the copy at path if it is Speecher's and no one changed it.
    const auto removeCopy = [&](const QString &path) {
        if (ownerOfFile(path) != Owner::Speecher || QFile::remove(path)) {
            return true;
        }
        if (error) {
            *error = QStringLiteral("Could not delete %1").arg(path);
        }
        return false;
    };
    QSet<QString> seen;
    for (const QString &dataDir : dataDirs.mid(1)) {
        const QDir applications(dataDir + QStringLiteral("/applications"));
        if (applications == userApplications) {
            continue;
        }
        for (const QString &name : desktopEntries(applications)) {
            if (seen.contains(name)) {
                continue;
            }
            seen.insert(name);
            // An entry that can't be read leaves its copy as it is.
            QFile entry(applications.filePath(name));
            if (!entry.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QByteArray copy = accessibleCopy(entry.readAll());
            const QString target = userApplications.filePath(name);
            // A copy of an entry that no longer starts a Chromium app, or is
            // no longer a desktop entry, would go on standing in for it.
            if (copy.isEmpty()) {
                if (!removeCopy(target)) {
                    return false;
                }
                continue;
            }
            if (!QDir().mkpath(userApplications.path()) || !writeCopy(target, copy, error)) {
                return false;
            }
        }
    }
    // A copy of an entry that is gone would keep showing an uninstalled app.
    for (const QString &name : desktopEntries(userApplications)) {
        if (!seen.contains(name) && !removeCopy(userApplications.filePath(name))) {
            return false;
        }
    }
    return true;
}

bool hasAccessibleLaunchers(const QString &dataHome)
{
    const QDir applications(dataHome + QStringLiteral("/applications"));
    const QStringList names = desktopEntries(applications);
    return std::any_of(names.cbegin(), names.cend(), [&](const QString &name) {
        return ownerOfFile(applications.filePath(name)) == Owner::Speecher;
    });
}

void removeAccessibleLaunchers(const QString &dataHome, DesktopIntegrationRemoval &removal)
{
    const QString name = QStringLiteral("Chromium app launchers");
    const QDir applications(dataHome + QStringLiteral("/applications"));
    bool removedAny = false;
    bool foundAny = false;
    for (const QString &entryName : desktopEntries(applications)) {
        const QString path = applications.filePath(entryName);
        switch (ownerOfFile(path)) {
        case Owner::Someone:
            break;
        case Owner::ChangedSinceSpeecher:
            foundAny = true;
            removal.failed.append(QStringLiteral("%1: %2 has changed since Speecher wrote it").arg(name, path));
            break;
        case Owner::Speecher:
            foundAny = true;
            if (QFile::remove(path)) {
                removedAny = true;
            } else {
                removal.failed.append(QStringLiteral("%1: could not delete %2").arg(name, path));
            }
            break;
        }
    }
    if (removedAny) {
        removal.removed.append(name);
    } else if (!foundAny) {
        removal.absent.append(name);
    }
}

} // namespace speecher
