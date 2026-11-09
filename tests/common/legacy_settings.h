#pragma once

#include "core/settings/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSettings>
#include <QString>

// Writes settings as older releases stored them, for the migration tests.
// Speecher itself only reads these keys.

namespace speecher::test {

// The paste methods' names, as output/pasteRules and the first Apps
// settings stored them.
namespace PasteMethod {
inline const QString StandardPaste = QStringLiteral("standard_paste");
inline const QString TerminalPaste = QStringLiteral("terminal_paste");
inline const QString DirectInsert = QStringLiteral("direct_insert");
inline const QString ClipboardOnly = QStringLiteral("clipboard_only");
} // namespace PasteMethod

// A paste rule as output/pasteRules stored it.
struct LegacyPasteRule {
    enum class Scope { Global, Category, Application };

    Scope scope = Scope::Global;
    // An app type's name, or an application id.
    QString match;
    // One of PasteMethod's names.
    QString method = PasteMethod::StandardPaste;
    bool enabled = true;
};

inline QString legacyScopeName(LegacyPasteRule::Scope scope)
{
    switch (scope) {
    case LegacyPasteRule::Scope::Application:
        return QStringLiteral("application");
    case LegacyPasteRule::Scope::Category:
        return QStringLiteral("category");
    case LegacyPasteRule::Scope::Global:
        break;
    }
    return QStringLiteral("global");
}

inline void storeLegacyPasteRules(QSettings &settings, const QList<LegacyPasteRule> &rules)
{
    QJsonArray array;
    for (const LegacyPasteRule &rule : rules) {
        array.append(QJsonObject{
            {QStringLiteral("scope"), legacyScopeName(rule.scope)},
            {QStringLiteral("match"), rule.match.trimmed()},
            {QStringLiteral("method"), rule.method},
            {QStringLiteral("enabled"), rule.enabled},
        });
    }
    settings.setValue(SettingsKeys::PasteRules, QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace speecher::test
