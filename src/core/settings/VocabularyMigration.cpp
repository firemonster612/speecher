#include "core/settings/VocabularyMigration.h"

#include "core/BindingProcessor.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/VocabularySettingsCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {

namespace {

constexpr int vocabularyMigrationVersion = 1;

QJsonArray storedArray(const QSettings &settings, const QString &key)
{
    return QJsonDocument::fromJson(settings.value(key).toByteArray()).array();
}

QList<VocabularyEntry> legacyTerms(const QSettings &settings)
{
    QList<VocabularyEntry> terms;
    const QJsonDocument stored = QJsonDocument::fromJson(settings.value(SettingsKeys::VocabularyEntries).toByteArray());
    if (!stored.isArray()) {
        for (const QString &term : settings.value(SettingsKeys::LegacyVocabulary).toStringList()) {
            terms.append({term, QStringLiteral("legacy")});
        }
        return terms;
    }
    for (const QJsonValue &value : stored.array()) {
        const QJsonObject object = value.toObject();
        VocabularyEntry entry;
        entry.term = object.value(QStringLiteral("term")).toString();
        entry.source = object.value(QStringLiteral("source")).toString(QStringLiteral("manual"));
        entry.starred = object.value(QStringLiteral("starred")).toBool(false);
        entry.frequency = object.value(QStringLiteral("frequency")).toInt(0);
        entry.lastUsedMs = qint64(object.value(QStringLiteral("lastUsedMs")).toDouble());
        entry.context = object.value(QStringLiteral("context")).toString();
        for (const QJsonValue &profile : object.value(QStringLiteral("profiles")).toArray()) {
            entry.profiles.append(profile.toString());
        }
        entry.keyTerm = object.value(QStringLiteral("keyTerm")).toBool(true);
        terms.append(entry);
    }
    return terms;
}

QList<VocabularyEntry> legacyReplacements(const QSettings &settings)
{
    QList<BindingRule> rules;
    for (const QJsonValue &value : storedArray(settings, SettingsKeys::BindingRules)) {
        const QJsonObject object = value.toObject();
        rules.append({object.value(QStringLiteral("phrase")).toString().trimmed(),
                      object.value(QStringLiteral("replacement")).toString()});
    }
    // As the older release loaded them: the first of two with one phrase.
    QList<VocabularyEntry> replacements;
    for (const BindingRule &rule : BindingProcessor::validateRules(rules).rules) {
        VocabularyEntry entry;
        entry.term = rule.phrase;
        entry.replacement = rule.replacement;
        replacements.append(entry);
    }
    return replacements;
}

QList<VocabularyEntry> legacyCorrections(const QSettings &settings)
{
    QList<VocabularyEntry> corrections;
    for (const QJsonValue &value : storedArray(settings, SettingsKeys::LearnedCorrections)) {
        const QJsonObject object = value.toObject();
        VocabularyEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.term = object.value(QStringLiteral("original")).toString();
        entry.replacement = object.value(QStringLiteral("corrected")).toString();
        entry.source = QStringLiteral("learned");
        const QString app = object.value(QStringLiteral("applicationId")).toString();
        if (!app.isEmpty()) {
            entry.apps = {app};
        }
        entry.createdAtMs = qint64(object.value(QStringLiteral("createdAtMs")).toDouble());
        entry.confidence = object.value(QStringLiteral("confidence")).toDouble();
        entry.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        entry.evidenceCount = object.value(QStringLiteral("evidenceCount")).toInt(1);
        entry.lastObservedAtMs = qint64(object.value(QStringLiteral("lastObservedAtMs")).toDouble(double(entry.createdAtMs)));
        if (!entry.id.isEmpty() && !entry.term.trimmed().isEmpty() && !entry.replacement.trimmed().isEmpty()) {
            corrections.append(entry);
        }
    }
    return corrections;
}

} // namespace

void migrateVocabulary(QSettings &settings)
{
    if (settings.value(SettingsKeys::VocabularyMigrationVersion).toInt() >= vocabularyMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::VocabularyMigrationVersion, vocabularyMigrationVersion);
    if (settings.contains(SettingsKeys::Vocabulary)) {
        return;
    }
    if (!settings.contains(SettingsKeys::VocabularyEntries) && !settings.contains(SettingsKeys::LegacyVocabulary)
        && !settings.contains(SettingsKeys::BindingRules) && !settings.contains(SettingsKeys::LearnedCorrections)) {
        return;
    }
    VocabularySettingsCodec::store(settings,
                                   legacyTerms(settings) + legacyReplacements(settings) + legacyCorrections(settings));
}

} // namespace speecher
