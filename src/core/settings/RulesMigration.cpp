#include "core/settings/RulesMigration.h"

#include "core/WritingProfileEdits.h"
#include "core/settings/SettingsCodecs.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/StoredJson.h"

#include <QJsonObject>

#include <algorithm>

namespace speecher {

namespace {

constexpr int rulesMigrationVersion = 1;

// name, or name and the first of " 2", " 3" and so on that no rule goes by,
// ignoring case.
QString freeName(const QString &name, const QList<RefinementRule> &rules)
{
    const auto taken = [&rules](const QString &candidate) {
        return std::any_of(rules.cbegin(), rules.cend(), [&candidate](const RefinementRule &rule) {
            return rule.name.compare(candidate, Qt::CaseInsensitive) == 0;
        });
    };
    QString free = name;
    for (int n = 2; taken(free); ++n) {
        free = name + QLatin1Char(' ') + QString::number(n);
    }
    return free;
}

// What the editor called a stored profile: a built-in's label or its name.
QString profileLabel(const QJsonObject &profile)
{
    const QString id = writingProfileFromName(profile.value(QStringLiteral("profile")).toString());
    return isBuiltInWritingProfile(id) ? writingProfileLabel(id, {})
                                       : profile.value(QStringLiteral("name")).toString().trimmed();
}

// Turns rule `id` on in the stored profile.
void switchOn(QJsonObject &profile, const QString &id)
{
    QJsonObject switches = profile.value(QStringLiteral("rules")).toObject();
    switches.insert(id, true);
    profile.insert(QStringLiteral("rules"), switches);
}

} // namespace

void migrateRules(QSettings &settings)
{
    if (settings.value(SettingsKeys::RulesMigrationVersion).toInt() >= rulesMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::RulesMigrationVersion, rulesMigrationVersion);
    if (settings.contains(SettingsKeys::RefinementRules)) {
        return;
    }

    QList<RefinementRule> rules;
    // The id names what the rule came from, so the phone, which migrates the
    // same settings it imported, makes the same id, and a later import
    // replaces its rule instead of adding a copy.
    const auto addRule = [&rules](const QString &source, const QString &name, const QString &instruction,
                                  bool everyProfile) {
        QStringList ids;
        for (const RefinementRule &rule : std::as_const(rules)) {
            ids.append(rule.id);
        }
        const QString id = QStringLiteral("custom_rule_") + source;
        rules.append({ids.contains(id) ? newCustomId(QStringLiteral("custom_rule"), ids) : id,
                      freeName(name.trimmed(), rules), instruction.trimmed(), everyProfile});
        return rules.last().id;
    };
    QJsonArray profiles = storedArray(settings, SettingsKeys::WritingProfiles);
    // Turns rule `id` on in each profile whose `field` is `chosen`, and sets
    // that field to `instead`.
    const auto replaceChoice = [&profiles](const QString &id, const QString &field, const QString &chosen,
                                           const QString &instead) {
        for (QJsonValueRef item : profiles) {
            QJsonObject profile = item.toObject();
            if (profile.value(field).toString().trimmed().toLower() == chosen) {
                switchOn(profile, id);
                profile.insert(field, instead);
                item = profile;
            }
        }
    };

    const QString global = settings.value(SettingsKeys::AdditionalInstructions).toString();
    if (!global.trimmed().isEmpty()) {
        addRule(QStringLiteral("every_profile"), QStringLiteral("Instructions for every profile"), global, true);
    }
    for (QJsonValueRef item : profiles) {
        QJsonObject profile = item.toObject();
        const QString instructions = profile.value(QStringLiteral("instructions")).toString();
        if (!instructions.trimmed().isEmpty()) {
            const QString id = writingProfileFromName(profile.value(QStringLiteral("profile")).toString());
            switchOn(profile, addRule(id + QStringLiteral("_instructions"),
                                      profileLabel(profile) + QStringLiteral(" instructions"), instructions, false));
            item = profile;
        }
    }
    for (const QJsonValue &item : storedArray(settings, SettingsKeys::CustomTones)) {
        const QJsonObject tone = item.toObject();
        const QString name = tone.value(QStringLiteral("name")).toString().trimmed();
        const QString instruction = QStringLiteral("Write in this tone: %1. %2")
                                        .arg(name, tone.value(QStringLiteral("instruction")).toString().trimmed());
        const QString id = tone.value(QStringLiteral("id")).toString();
        replaceChoice(addRule(id, name + QStringLiteral(" tone"), instruction, false), QStringLiteral("tone"), id,
                      QStringLiteral("none"));
    }
    for (const QJsonValue &item : storedArray(settings, SettingsKeys::CustomCleanupLevels)) {
        const QJsonObject level = item.toObject();
        const QString base = level.value(QStringLiteral("base")).toString();
        const QString id = level.value(QStringLiteral("id")).toString();
        replaceChoice(addRule(id, level.value(QStringLiteral("name")).toString(),
                              level.value(QStringLiteral("instructions")).toString(), false),
                      QStringLiteral("cleanupStrength"), id,
                      base == QStringLiteral("custom_only") ? QStringLiteral("light_cleanup") : base);
    }

    if (rules.isEmpty()) {
        return;
    }
    settings.setValue(SettingsKeys::RefinementRules, QJsonDocument(refinementRulesJson(rules)).toJson(QJsonDocument::Compact));
    // Profiles never saved still follow the cleanup level an older release
    // kept, so none are written for them.
    if (settings.contains(SettingsKeys::WritingProfiles)) {
        settings.setValue(SettingsKeys::WritingProfiles, QJsonDocument(profiles).toJson(QJsonDocument::Compact));
    }
}

} // namespace speecher
