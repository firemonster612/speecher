#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

inline QStringList settingsPageNames()
{
    return {
        QStringLiteral("general"),
        QStringLiteral("audio"),
        QStringLiteral("output"),
        QStringLiteral("accounts"),
        QStringLiteral("refinement"),
        QStringLiteral("vocabulary"),
    };
}

// Returns the current page id for a command-line or IPC name. Removed page
// names keep working and follow the settings that replaced them.
inline QString canonicalSettingsPageName(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (settingsPageNames().contains(key)) {
        return key;
    }
    if (key == QStringLiteral("applications") || key == QStringLiteral("apps")) {
        return QStringLiteral("output");
    }
    if (key == QStringLiteral("auth") || key == QStringLiteral("providers")) {
        return QStringLiteral("accounts");
    }
    if (key == QStringLiteral("corrections") || key == QStringLiteral("bindings")) {
        return QStringLiteral("vocabulary");
    }
    return {};
}

} // namespace speecher
