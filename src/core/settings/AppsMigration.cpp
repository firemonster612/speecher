#include "core/settings/AppsMigration.h"

#include "core/Apps.h"
#include "core/settings/AppsSettingsCodec.h"
#include "core/settings/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {

namespace {

constexpr int appsMigrationVersion = 1;

QString scopeName(LegacyPasteRule::Scope scope)
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

LegacyPasteRule::Scope scopeFromName(const QString &name)
{
    if (name == u"application") return LegacyPasteRule::Scope::Application;
    if (name == u"category") return LegacyPasteRule::Scope::Category;
    return LegacyPasteRule::Scope::Global;
}

QJsonArray storedArray(const QSettings &settings, const QString &key)
{
    return QJsonDocument::fromJson(settings.value(key).toByteArray()).array();
}

// What an App migrated from a bare id or match text is called: the text, or
// an id's last dotted part, capitalized.
QString nameFrom(const QString &text)
{
    QString name = text.section(QLatin1Char('.'), -1).trimmed();
    if (name.isEmpty()) {
        name = text.trimmed();
    }
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

QStringList idsOf(const QList<AppEntry> &apps)
{
    QStringList ids;
    for (const AppEntry &entry : apps) {
        ids.append(entry.id);
    }
    return ids;
}

// The App matched by exactly this id, added where there is none.
AppEntry &appForId(QList<AppEntry> &apps, const QString &id)
{
    const QString comparable = comparableAppId(id);
    for (AppEntry &entry : apps) {
        for (const AppMatch &match : entry.matches) {
            if (match.kind == AppMatchKind::Id && comparableAppId(match.value) == comparable) {
                return entry;
            }
        }
    }
    apps.append({newAppId(idsOf(apps)), nameFrom(id), {}, {{AppMatchKind::Id, id.trimmed()}}, {}, {}, {}});
    return apps.last();
}

} // namespace

QList<LegacyPasteRule> legacyPasteRules(const QSettings &settings)
{
    const QJsonDocument document = QJsonDocument::fromJson(settings.value(SettingsKeys::PasteRules).toByteArray());
    if (!document.isArray()) {
        if (!terminalKeysDiffer) {
            return {};
        }
        return {{LegacyPasteRule::Scope::Category, appCategoryName(AppCategory::Terminal), PasteMethod::TerminalPaste,
                 true}};
    }
    QList<LegacyPasteRule> rules;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        rules.append({scopeFromName(object.value(QStringLiteral("scope")).toString()),
                      object.value(QStringLiteral("match")).toString().trimmed(),
                      pasteMethodFromName(object.value(QStringLiteral("method")).toString()),
                      object.value(QStringLiteral("enabled")).toBool(true)});
    }
    return rules;
}

void storeLegacyPasteRules(QSettings &settings, const QList<LegacyPasteRule> &rules)
{
    QJsonArray array;
    for (const LegacyPasteRule &rule : rules) {
        array.append(QJsonObject{
            {QStringLiteral("scope"), scopeName(rule.scope)},
            {QStringLiteral("match"), rule.match.trimmed()},
            {QStringLiteral("method"), pasteMethodName(rule.method)},
            {QStringLiteral("enabled"), rule.enabled},
        });
    }
    settings.setValue(SettingsKeys::PasteRules, QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void migrateApps(QSettings &settings)
{
    if (settings.value(SettingsKeys::AppsMigrationVersion).toInt() >= appsMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::AppsMigrationVersion, appsMigrationVersion);
    const bool storedPasteRules = settings.contains(SettingsKeys::PasteRules);
    if (!storedPasteRules && !settings.contains(SettingsKeys::WritingProfileOverrides)
        && !settings.contains(SettingsKeys::AppRecognitionRules)
        && !settings.contains(SettingsKeys::DefaultWritingProfile)) {
        return;
    }

    AppsSettings apps;
    // An earlier override or rule for the same app won, so a later one sets
    // only what is still unset.
    for (const QJsonValue &value : storedArray(settings, SettingsKeys::WritingProfileOverrides)) {
        const QJsonObject override = value.toObject();
        const QString id = override.value(QStringLiteral("applicationId")).toString().trimmed();
        if (id.isEmpty() || !override.value(QStringLiteral("enabled")).toBool(true)) {
            continue;
        }
        AppEntry &entry = appForId(apps.apps, id);
        if (!entry.writingProfile) {
            entry.writingProfile = writingProfileFromName(override.value(QStringLiteral("profile")).toString());
        }
    }

    const QList<LegacyPasteRule> pasteRules = legacyPasteRules(settings);
    for (const LegacyPasteRule &rule : pasteRules) {
        if (rule.scope != LegacyPasteRule::Scope::Application || !rule.enabled || rule.match.isEmpty()) {
            continue;
        }
        AppEntry &entry = appForId(apps.apps, rule.match);
        if (!entry.pasting) {
            entry.pasting = rule.method;
        }
    }

    for (const QJsonValue &value : storedArray(settings, SettingsKeys::AppRecognitionRules)) {
        const QJsonObject rule = value.toObject();
        const QString match = rule.value(QStringLiteral("match")).toString().trimmed();
        const AppCategory category = appCategoryFromName(rule.value(QStringLiteral("category")).toString());
        const QString profile = rule.value(QStringLiteral("writingProfile")).toString();
        if (match.isEmpty() || (category == AppCategory::Unknown && profile.isEmpty())) {
            continue;
        }
        AppEntry entry{newAppId(idsOf(apps.apps)), nameFrom(match), {}, {{AppMatchKind::Name, match}}, {}, {}, {}};
        if (category != AppCategory::Unknown) {
            entry.type = category;
        }
        if (!profile.isEmpty()) {
            entry.writingProfile = writingProfileFromName(profile);
            entry.matches.append({AppMatchKind::WindowTitle, match});
        }
        apps.apps.append(entry);
    }

    if (storedPasteRules) {
        for (AppTypeSettings &type : apps.types) {
            type.pasting.reset();
            for (const LegacyPasteRule &rule : pasteRules) {
                if (rule.scope == LegacyPasteRule::Scope::Category && rule.enabled
                    && appCategoryFromName(rule.match) == type.type) {
                    // Standard paste was the same as no rule.
                    if (rule.method != PasteMethod::StandardPaste) {
                        type.pasting = rule.method;
                    }
                    break;
                }
            }
        }
    }

    if (settings.contains(SettingsKeys::DefaultWritingProfile)) {
        for (AppTypeSettings &type : apps.types) {
            if (type.type == AppCategory::General) {
                type.writingProfile = writingProfileFromName(settings.value(SettingsKeys::DefaultWritingProfile).toString());
            }
        }
    }
    AppsSettingsCodec::store(settings, apps);
}

} // namespace speecher
