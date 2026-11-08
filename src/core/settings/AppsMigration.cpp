#include "core/settings/AppsMigration.h"

#include "core/Apps.h"
#include "core/settings/AppsSettingsCodec.h"
#include "core/settings/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMap>

#include <algorithm>

namespace speecher {

namespace {

// 1 moved older releases' per-app settings into Apps; 2 turned Pasting
// choices into steps and keys.
constexpr int appsMigrationVersion = 2;

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

// The Pasting page's steps as stored, after migrateDeliverySteps.
DeliverySteps storedSteps(const QSettings &settings)
{
    return {settings.value(SettingsKeys::InsertThroughAccessibility, true).toBool(),
            settings.value(SettingsKeys::PressPasteKeys, true).toBool(),
            settings.value(SettingsKeys::KeepOnClipboard, true).toBool()};
}

// What an older Pasting choice becomes, so a delivery takes the steps and
// keys it took. One that followed the steps as they are leaves them unset
// where unset still reaches the Pasting page's (stepsFollow), and takes a
// copy of them otherwise. A narrowing that left no step on, which delivered
// nothing, keeps the words on the clipboard.
PastingChoice pastingFrom(PasteMethod method, const DeliverySteps &steps, bool stepsFollow)
{
    const std::optional<DeliverySteps> asTheyAre = stepsFollow ? std::nullopt : std::optional(steps);
    DeliverySteps narrowed = steps;
    switch (method) {
    case PasteMethod::StandardPaste:
        return {asTheyAre, PasteKeyCombo::Standard};
    case PasteMethod::TerminalPaste:
        return {asTheyAre, terminalKeysDiffer ? PasteKeyCombo::Terminal : PasteKeyCombo::Standard};
    case PasteMethod::ClipboardOnly:
        narrowed.insert = false;
        [[fallthrough]];
    case PasteMethod::DirectInsert:
        narrowed.press = false;
        break;
    }
    if (!narrowed.any()) {
        narrowed.keep = true;
    }
    return {narrowed, std::nullopt};
}

// Gives the Apps and App Types the older Pasting choices, by App id and by
// App Type. Steps and keys now settle each on their own, so a choice that
// followed the steps copies them wherever a later layer, Terminals' or a
// type's, has steps of its own its old choice used to hide.
void applyPastingChoices(AppsSettings &apps, const QHash<QString, PasteMethod> &forApps,
                         const QMap<AppCategory, PasteMethod> &forTypes, const DeliverySteps &steps)
{
    const auto typeHasSteps = [&apps](AppCategory except) {
        return std::any_of(apps.types.cbegin(), apps.types.cend(), [except](const AppTypeSettings &type) {
            return type.type != except && type.pasting.steps;
        });
    };
    // Terminals come before the type of anything running in a terminal, so
    // they go last among the types.
    for (const bool terminals : {false, true}) {
        for (AppTypeSettings &type : apps.types) {
            if ((type.type == AppCategory::Terminal) == terminals && forTypes.contains(type.type)) {
                type.pasting = pastingFrom(forTypes.value(type.type), steps,
                                           !terminals || !typeHasSteps(AppCategory::Terminal));
            }
        }
    }
    const bool stepsFollow = !typeHasSteps(AppCategory::Unknown);
    for (AppEntry &entry : apps.apps) {
        if (forApps.contains(entry.id)) {
            entry.pasting = pastingFrom(forApps.value(entry.id), steps, stepsFollow);
        }
    }
}

// Version 2 over settings version 1 wrote: each App's and App Type's
// "pasting" becomes steps and keys.
void migratePastingChoices(QSettings &settings)
{
    if (!settings.contains(SettingsKeys::AppsEntries) && !settings.contains(SettingsKeys::AppsTypes)) {
        return;
    }
    const QString pasting = QStringLiteral("pasting");
    QHash<QString, PasteMethod> forApps;
    for (const QJsonValue &value : storedArray(settings, SettingsKeys::AppsEntries)) {
        const QString method = value[pasting].toString();
        if (!method.isEmpty()) {
            forApps.insert(value[QStringLiteral("id")].toString(), pasteMethodFromName(method));
        }
    }
    QMap<AppCategory, PasteMethod> forTypes;
    if (settings.contains(SettingsKeys::AppsTypes)) {
        for (const QJsonValue &value : storedArray(settings, SettingsKeys::AppsTypes)) {
            const QString method = value[pasting].toString();
            // "The steps" was no choice of its own for an App Type.
            if (!method.isEmpty() && pasteMethodFromName(method) != PasteMethod::StandardPaste) {
                forTypes.insert(appCategoryFromName(value[QStringLiteral("type")].toString()), pasteMethodFromName(method));
            }
        }
    } else if (terminalKeysDiffer) {
        // Unstored types were the defaults, Terminals' keys among them.
        forTypes.insert(AppCategory::Terminal, PasteMethod::TerminalPaste);
    }
    AppsSettings apps = AppsSettingsCodec::load(settings);
    for (AppTypeSettings &type : apps.types) {
        type.pasting = {};
    }
    applyPastingChoices(apps, forApps, forTypes, storedSteps(settings));
    AppsSettingsCodec::store(settings, apps);
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
    const int version = settings.value(SettingsKeys::AppsMigrationVersion).toInt();
    if (version >= appsMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::AppsMigrationVersion, appsMigrationVersion);
    if (version == 1) {
        migratePastingChoices(settings);
        return;
    }
    const bool storedPasteRules = settings.contains(SettingsKeys::PasteRules);
    if (!storedPasteRules && !settings.contains(SettingsKeys::WritingProfileOverrides)
        && !settings.contains(SettingsKeys::AppRecognitionRules)
        && !settings.contains(SettingsKeys::DefaultWritingProfile)) {
        return;
    }

    AppsSettings apps;
    QHash<QString, PasteMethod> appPasting;
    QMap<AppCategory, PasteMethod> typePasting;
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
        const QString id = appForId(apps.apps, rule.match).id;
        if (!appPasting.contains(id)) {
            appPasting.insert(id, rule.method);
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

    // Unstored rules were the default, Terminals' keys (legacyPasteRules).
    for (AppTypeSettings &type : apps.types) {
        type.pasting = {};
        for (const LegacyPasteRule &rule : pasteRules) {
            if (rule.scope == LegacyPasteRule::Scope::Category && rule.enabled
                && appCategoryFromName(rule.match) == type.type) {
                // Standard paste was the same as no rule.
                if (rule.method != PasteMethod::StandardPaste) {
                    typePasting.insert(type.type, rule.method);
                }
                break;
            }
        }
    }
    applyPastingChoices(apps, appPasting, typePasting, storedSteps(settings));

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
