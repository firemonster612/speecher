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
bool isFieldCode(const QString &argument)
{
    return (argument.size() == 2 && argument.startsWith(QLatin1Char('%')))
        || argument.startsWith(QStringLiteral("@@"));
}

bool holdsChromium(const QDir &directory)
{
    return directory.exists(QStringLiteral("chrome_100_percent.pak"))
        || directory.exists(QStringLiteral("resources.pak"))
        || directory.exists(QStringLiteral("resources/app.asar"));
}

QString firstExecLine(const QByteArray &entry)
{
    for (const QByteArray &line : entry.split('\n')) {
        if (line.startsWith("Exec=")) {
            return QString::fromUtf8(line.mid(5));
        }
    }
    return {};
}

QByteArray checksum(const QByteArray &contents)
{
    return QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
}

// The entry with withAccessibilityFlag() on each Exec line and Speecher's
// mark under its group header, or nothing when it has no such header.
QByteArray accessibleCopy(const QByteArray &entry)
{
    QList<QByteArray> lines = entry.split('\n');
    const qsizetype header = lines.indexOf(desktopEntryGroup);
    if (header < 0) {
        return {};
    }
    for (QByteArray &line : lines) {
        if (line.startsWith("Exec=")) {
            line = "Exec=" + withAccessibilityFlag(QString::fromUtf8(line.mid(5))).toUtf8();
        }
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

QStringList desktopEntries(const QDir &directory)
{
    return directory.entryList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
}

} // namespace

bool launchesChromium(const QString &execLine)
{
    const QList<ExecArgument> arguments = execArguments(execLine);
    if (arguments.isEmpty()) {
        return false;
    }
    const QString program = arguments.first().text;
    if (QFileInfo(program).fileName() == QLatin1String("flatpak")) {
        const auto run = std::find_if(arguments.cbegin(), arguments.cend(),
                                      [](const ExecArgument &a) { return a.text == QLatin1String("run"); });
        const auto appId = std::find_if(run, arguments.cend(), [](const ExecArgument &a) {
            return a.text != QLatin1String("run") && !a.text.startsWith(QLatin1Char('-'));
        });
        return appId != arguments.cend() && chromiumFlatpaks().contains(appId->text);
    }
    if (chromiumWrappers().contains(QFileInfo(program).fileName())) {
        return true;
    }
    const QString path = QFileInfo(program).isAbsolute() ? program : QStandardPaths::findExecutable(program);
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
    const auto fieldCode = std::find_if(arguments.cbegin(), arguments.cend(),
                                        [](const ExecArgument &a) { return isFieldCode(a.text); });
    if (fieldCode == arguments.cend()) {
        return execLine + QLatin1Char(' ') + accessibilityFlag;
    }
    return QString(execLine).insert(fieldCode->start, accessibilityFlag + QLatin1Char(' '));
}

bool writeAccessibleLaunchers(const QStringList &dataDirs, QString *error)
{
    if (dataDirs.isEmpty()) {
        return true;
    }
    const QDir userApplications(dataDirs.first() + QStringLiteral("/applications"));
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
            const QByteArray entry = readFile(applications.filePath(name));
            if (!launchesChromium(firstExecLine(entry))) {
                continue;
            }
            const QString target = userApplications.filePath(name);
            if (QFile::exists(target) && ownerOf(readFile(target)) != Owner::Speecher) {
                continue;
            }
            const QByteArray copy = accessibleCopy(entry);
            if (copy.isEmpty()) {
                continue;
            }
            QSaveFile file(target);
            if (!QDir().mkpath(userApplications.path()) || !file.open(QIODevice::WriteOnly)
                || file.write(copy) != copy.size() || !file.commit()) {
                if (error) {
                    *error = QStringLiteral("Could not write %1: %2").arg(target, file.errorString());
                }
                return false;
            }
        }
    }
    return true;
}

void removeAccessibleLaunchers(const QString &dataHome, DesktopIntegrationRemoval &removal)
{
    const QString name = QStringLiteral("Chromium app launchers");
    const QDir applications(dataHome + QStringLiteral("/applications"));
    bool removedAny = false;
    bool foundAny = false;
    for (const QString &entryName : desktopEntries(applications)) {
        const QString path = applications.filePath(entryName);
        switch (ownerOf(readFile(path))) {
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
