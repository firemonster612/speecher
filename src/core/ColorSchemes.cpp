#include "core/ColorSchemes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>

#include <algorithm>

namespace speecher {

namespace {

// group -> key -> value, from an INI file in KConfig's form. Localized keys
// such as Name[de] are kept apart from Name, so they never replace it.
using IniGroups = QHash<QString, QHash<QString, QString>>;

IniGroups readIni(const QString &path)
{
    IniGroups groups;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return groups;
    }
    QTextStream in(&file);
    QString group;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals > 0) {
            groups[group].insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    return groups;
}

// KConfig writes a colour as "r,g,b", "r,g,b,a" or a name such as "#3daee9".
QColor parseColor(const QString &value)
{
    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() == 3 || parts.size() == 4) {
        int channels[4] = {0, 0, 0, 255};
        for (qsizetype i = 0; i < parts.size(); ++i) {
            bool ok = false;
            channels[i] = parts.at(i).trimmed().toInt(&ok);
            if (!ok || channels[i] < 0 || channels[i] > 255) {
                return {};
            }
        }
        return QColor(channels[0], channels[1], channels[2], channels[3]);
    }
    return QColor::fromString(value);
}

QColor color(const IniGroups &groups, const char *group, const char *key)
{
    return parseColor(groups.value(QLatin1String(group)).value(QLatin1String(key)));
}

} // namespace

ColorScheme readKdeColorScheme(const QString &path)
{
    const IniGroups groups = readIni(path);
    const QHash<QString, QString> general = groups.value(QStringLiteral("General"));

    ColorScheme scheme;
    scheme.id = QFileInfo(path).completeBaseName();
    scheme.name = general.value(QStringLiteral("Name"), scheme.id);
    scheme.window = color(groups, "Colors:Window", "BackgroundNormal");
    scheme.text = color(groups, "Colors:Window", "ForegroundNormal");
    // kdeglobals carries AccentColor when the desktop's accent is not the
    // scheme's own selection colour.
    const QColor accent = parseColor(general.value(QStringLiteral("AccentColor")));
    scheme.accent = accent.isValid() ? accent : color(groups, "Colors:Selection", "BackgroundNormal");
    scheme.negative = color(groups, "Colors:View", "ForegroundNegative");
    return scheme;
}

QString colorSchemesFolder(const QString &dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("color-schemes"));
}

QList<ColorScheme> installedColorSchemes(const QStringList &dataDirs)
{
    QList<ColorScheme> schemes;
    QStringList seen;
    for (const QString &dataDir : dataDirs) {
        const QDir folder(colorSchemesFolder(dataDir));
        for (const QFileInfo &file : folder.entryInfoList({QStringLiteral("*.colors")}, QDir::Files, QDir::Name)) {
            if (seen.contains(file.completeBaseName())) {
                continue;
            }
            seen.append(file.completeBaseName());
            const ColorScheme scheme = readKdeColorScheme(file.filePath());
            if (scheme.isValid()) {
                schemes.append(scheme);
            }
        }
    }
    std::sort(schemes.begin(), schemes.end(), [](const ColorScheme &a, const ColorScheme &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return schemes;
}

} // namespace speecher
