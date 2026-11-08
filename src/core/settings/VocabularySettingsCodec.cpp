#include "core/settings/VocabularySettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include "core/BindingProcessor.h"
#include "core/Vocabulary.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher::VocabularySettingsCodec {
namespace {

QStringList strings(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray()) {
        list.append(item.toString());
    }
    return list;
}

// A replacement the dictation's matcher would refuse can't be kept.
bool usable(const VocabularyEntry &entry)
{
    return !entry.isReplacement() || BindingProcessor::validateRules({{entry.term, entry.replacement}}).ok();
}

QList<VocabularyEntry> fromJson(const QJsonArray &array)
{
    QList<VocabularyEntry> entries;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        VocabularyEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.term = object.value(QStringLiteral("term")).toString();
        entry.replacement = object.value(QStringLiteral("replacement")).toString();
        entry.source = object.value(QStringLiteral("source")).toString(QStringLiteral("manual"));
        entry.starred = object.value(QStringLiteral("starred")).toBool(false);
        entry.frequency = object.value(QStringLiteral("frequency")).toInt(0);
        entry.lastUsedMs = qint64(object.value(QStringLiteral("lastUsedMs")).toDouble());
        entry.context = object.value(QStringLiteral("context")).toString();
        entry.profiles = strings(object.value(QStringLiteral("profiles")));
        entry.apps = strings(object.value(QStringLiteral("apps")));
        // Stored only when off, so every term saved before the setting
        // existed stays a key term.
        entry.keyTerm = object.value(QStringLiteral("keyTerm")).toBool(true);
        entry.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        entry.confidence = object.value(QStringLiteral("confidence")).toDouble();
        entry.evidenceCount = object.value(QStringLiteral("evidenceCount")).toInt();
        entry.createdAtMs = qint64(object.value(QStringLiteral("createdAtMs")).toDouble());
        entry.lastObservedAtMs = qint64(object.value(QStringLiteral("lastObservedAtMs")).toDouble());
        if (usable(entry)) {
            entries.append(entry);
        }
    }
    return withVocabularyIds(normalizeVocabularyEntries(entries));
}

} // namespace

QList<VocabularyEntry> load(const QSettings &settings)
{
    return fromJson(QJsonDocument::fromJson(settings.value(SettingsKeys::Vocabulary).toByteArray()).array());
}

void store(QSettings &settings, const QList<VocabularyEntry> &entries)
{
    QJsonArray array;
    for (const VocabularyEntry &entry : withVocabularyIds(normalizeVocabularyEntries(entries))) {
        if (!usable(entry)) {
            continue;
        }
        QJsonObject object{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("term"), entry.term},
            {QStringLiteral("source"), entry.source},
        };
        const auto insertWhen = [&object](bool set, const char *key, const QJsonValue &value) {
            if (set) {
                object.insert(QLatin1String(key), value);
            }
        };
        insertWhen(entry.isReplacement(), "replacement", entry.replacement);
        insertWhen(entry.starred, "starred", true);
        insertWhen(entry.frequency > 0, "frequency", entry.frequency);
        insertWhen(entry.lastUsedMs > 0, "lastUsedMs", double(entry.lastUsedMs));
        insertWhen(!entry.context.isEmpty(), "context", entry.context);
        insertWhen(!entry.profiles.isEmpty(), "profiles", QJsonArray::fromStringList(entry.profiles));
        insertWhen(!entry.apps.isEmpty(), "apps", QJsonArray::fromStringList(entry.apps));
        insertWhen(!entry.keyTerm, "keyTerm", false);
        insertWhen(!entry.enabled, "enabled", false);
        insertWhen(entry.isLearned(), "confidence", entry.confidence);
        insertWhen(entry.isLearned(), "evidenceCount", entry.evidenceCount);
        insertWhen(entry.createdAtMs > 0, "createdAtMs", double(entry.createdAtMs));
        insertWhen(entry.lastObservedAtMs > 0, "lastObservedAtMs", double(entry.lastObservedAtMs));
        array.append(object);
    }
    settings.setValue(SettingsKeys::Vocabulary, QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool recordUsage(QSettings &settings, const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return false;
    }
    QList<VocabularyEntry> entries = load(settings);
    bool changed = false;
    for (VocabularyEntry &entry : entries) {
        if (!entry.isReplacement() && containsVocabularyTerm(text, entry.term)) {
            ++entry.frequency;
            entry.lastUsedMs = QDateTime::currentMSecsSinceEpoch();
            changed = true;
        }
    }
    if (changed) {
        store(settings, entries);
    }
    return changed;
}

} // namespace speecher::VocabularySettingsCodec
