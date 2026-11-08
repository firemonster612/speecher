// The installed apps on Linux: the .desktop files in the XDG applications
// directories, Flatpak's and Snap's exports included.
#include "platform/InstalledApps.h"

#include "core/Apps.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <optional>

namespace speecher {

namespace {

// The keys of a file's [Desktop Entry] group, values unescaped.
QHash<QString, QString> desktopEntry(const QString &path)
{
    QFile file(path);
    QHash<QString, QString> entry;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(u'[')) {
            inEntry = line == u"[Desktop Entry]";
            continue;
        }
        const qsizetype equals = line.indexOf(u'=');
        if (!inEntry || line.startsWith(u'#') || equals <= 0) {
            continue;
        }
        QString value = line.mid(equals + 1).trimmed();
        value.replace(QStringLiteral("\\s"), QStringLiteral(" ")).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        entry.insert(line.left(equals).trimmed(), value);
    }
    return entry;
}

// The entry's name in this locale, as the spec looks it up: language,
// country and modifier first, then fewer of them, then the plain Name.
QString localizedName(const QHash<QString, QString> &entry)
{
    const QString locale = QLocale().name();
    const QString language = locale.section(u'_', 0, 0);
    for (const QString &key : {QStringLiteral("Name[%1]").arg(locale), QStringLiteral("Name[%1]").arg(language)}) {
        if (!entry.value(key).isEmpty()) {
            return entry.value(key);
        }
    }
    return entry.value(QStringLiteral("Name"));
}

QStringList listValue(const QHash<QString, QString> &entry, const QString &key)
{
    return entry.value(key).split(u';', Qt::SkipEmptyParts);
}

// The Exec line's words, as the spec quotes them, without field codes.
QStringList execArguments(const QString &exec)
{
    QStringList arguments;
    QString word;
    bool quoted = false;
    bool inWord = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == u'"') {
            quoted = !quoted;
            inWord = true;
        } else if (c == u'\\' && quoted && i + 1 < exec.size()) {
            word.append(exec.at(++i));
        } else if (c.isSpace() && !quoted) {
            if (inWord) {
                arguments.append(word);
            }
            word.clear();
            inWord = false;
        } else {
            word.append(c);
            inWord = true;
        }
    }
    if (inWord) {
        arguments.append(word);
    }
    arguments.removeIf([](const QString &argument) { return argument.startsWith(u'%') || argument.startsWith(u'@'); });
    return arguments;
}

// The ids the program an Exec line starts may report: its name, past an
// `env VAR=…` prefix; for `flatpak run`, the app id it runs and any
// --command it names.
QStringList execIds(const QString &exec)
{
    QStringList arguments = execArguments(exec);
    if (!arguments.isEmpty() && QFileInfo(arguments.first()).fileName() == u"env") {
        arguments.removeFirst();
        while (!arguments.isEmpty() && arguments.first().contains(u'=')) {
            arguments.removeFirst();
        }
    }
    if (arguments.isEmpty()) {
        return {};
    }
    const QString program = QFileInfo(arguments.first()).fileName();
    if (program != u"flatpak" || arguments.value(1) != u"run") {
        return {program};
    }
    QStringList ids;
    for (const QString &argument : arguments.mid(2)) {
        if (argument.startsWith(QStringLiteral("--command="))) {
            ids.append(QFileInfo(argument.section(u'=', 1)).fileName());
        } else if (!argument.startsWith(u'-')) {
            ids.prepend(argument);
            break;
        }
    }
    return ids;
}

bool installed(const QString &tryExec)
{
    if (tryExec.isEmpty()) {
        return true;
    }
    if (QDir::isAbsolutePath(tryExec)) {
        return QFileInfo(tryExec).isExecutable();
    }
    return !QStandardPaths::findExecutable(tryExec).isEmpty();
}

// Whether the entry shows on one of the current desktops.
bool showsOn(const QHash<QString, QString> &entry, const QStringList &currentDesktops)
{
    const QStringList only = listValue(entry, QStringLiteral("OnlyShowIn"));
    const QStringList excluded = listValue(entry, QStringLiteral("NotShowIn"));
    const auto current = [&currentDesktops](const QString &desktop) {
        return currentDesktops.contains(desktop, Qt::CaseInsensitive);
    };
    return (only.isEmpty() || std::any_of(only.cbegin(), only.cend(), current))
        && std::none_of(excluded.cbegin(), excluded.cend(), current);
}

std::optional<InstalledApp> appFrom(const QString &path, const QString &desktopId, const QStringList &currentDesktops)
{
    const QHash<QString, QString> entry = desktopEntry(path);
    const auto isTrue = [&entry](const char *key) { return entry.value(QLatin1String(key)) == u"true"; };
    if (entry.value(QStringLiteral("Type")) != u"Application" || isTrue("NoDisplay") || isTrue("Hidden")
        || !showsOn(entry, currentDesktops) || !installed(entry.value(QStringLiteral("TryExec")))) {
        return std::nullopt;
    }
    const QString name = localizedName(entry);
    if (name.isEmpty()) {
        return std::nullopt;
    }
    QStringList ids{desktopId};
    const QString windowClass = entry.value(QStringLiteral("StartupWMClass")).toCaseFolded();
    ids << windowClass << execIds(entry.value(QStringLiteral("Exec")));
    QStringList unique;
    QSet<QString> seen;
    for (const QString &id : std::as_const(ids)) {
        if (!id.isEmpty() && !seen.contains(comparableAppId(id))) {
            seen.insert(comparableAppId(id));
            unique.append(id);
        }
    }
    return InstalledApp{name, entry.value(QStringLiteral("Icon")), unique, path};
}

QStringList applicationDirs()
{
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    // Normally on XDG_DATA_DIRS already; not every session puts them there.
    for (const QString &exported : {QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
                                    QStringLiteral("/var/lib/flatpak/exports/share/applications"),
                                    QStringLiteral("/var/lib/snapd/desktop/applications")}) {
        if (!dirs.contains(exported)) {
            dirs.append(exported);
        }
    }
    return dirs;
}

} // namespace

QList<InstalledApp> desktopEntryApps(const QStringList &applicationDirs, const QStringList &currentDesktops)
{
    QList<InstalledApp> apps;
    QSet<QString> seenIds;
    for (const QString &dir : applicationDirs) {
        const QDir root(dir);
        QDirIterator files(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            QString desktopId = root.relativeFilePath(path);
            desktopId.chop(int(qstrlen(".desktop")));
            desktopId.replace(u'/', u'-');
            // An earlier directory's file, even a hidden one, wins.
            if (seenIds.contains(desktopId)) {
                continue;
            }
            seenIds.insert(desktopId);
            if (const std::optional<InstalledApp> app = appFrom(path, desktopId, currentDesktops)) {
                apps.append(*app);
            }
        }
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp &a, const InstalledApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return apps;
}

QList<InstalledApp> installedApps()
{
    return desktopEntryApps(applicationDirs(),
                            qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(u':', Qt::SkipEmptyParts));
}

QImage installedAppIcon(const QString &key, int size)
{
    if (key.isEmpty()) {
        return {};
    }
    const QIcon icon = QDir::isAbsolutePath(key) ? QIcon(key) : QIcon::fromTheme(key);
    return icon.isNull() ? QImage() : icon.pixmap(size, size).toImage();
}

} // namespace speecher
