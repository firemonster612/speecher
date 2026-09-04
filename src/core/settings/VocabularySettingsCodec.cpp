#include "core/settings/VocabularySettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include "core/Vocabulary.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher::VocabularySettingsCodec {

QList<VocabularyEntry> load(const QSettings &settings)
{
    const QByteArray stored = settings.value(SettingsKeys::VocabularyEntries, QByteArray()).toByteArray();
    const QJsonDocument document = QJsonDocument::fromJson(stored);
    QList<VocabularyEntry> entries;
    if (document.isArray()) {
        for (const QJsonValue &value : document.array()) {
            const QJsonObject object = value.toObject();
            entries.append({
                object.value(QStringLiteral("term")).toString(),
                object.value(QStringLiteral("source")).toString(QStringLiteral("manual")),
                object.value(QStringLiteral("starred")).toBool(false),
                object.value(QStringLiteral("frequency")).toInt(0),
                qint64(object.value(QStringLiteral("lastUsedMs")).toDouble()),
            });
        }
    } else {
        for (const QString &term : settings.value(
                 SettingsKeys::LegacyVocabulary,
                 QStringList()).toStringList()) {
            entries.append({term, QStringLiteral("legacy"), false, 0, 0});
        }
    }
    return normalizeVocabularyEntries(entries);
}

void store(QSettings &settings, const QList<VocabularyEntry> &entries)
{
    const QList<VocabularyEntry> normalized = normalizeVocabularyEntries(entries);
    QJsonArray array;
    QStringList legacyTerms;
    for (const VocabularyEntry &entry : normalized) {
        array.append(QJsonObject{
            {QStringLiteral("term"), entry.term},
            {QStringLiteral("source"), entry.source},
            {QStringLiteral("starred"), entry.starred},
            {QStringLiteral("frequency"), entry.frequency},
            {QStringLiteral("lastUsedMs"), double(entry.lastUsedMs)},
        });
        legacyTerms.append(entry.term);
    }
    settings.setValue(
        SettingsKeys::VocabularyEntries,
        QJsonDocument(array).toJson(QJsonDocument::Compact));
    settings.setValue(SettingsKeys::LegacyVocabulary, legacyTerms);
}

void recordUsage(QSettings &settings, const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    QList<VocabularyEntry> entries = load(settings);
    bool changed = false;
    for (VocabularyEntry &entry : entries) {
        if (containsVocabularyTerm(text, entry.term)) {
            ++entry.frequency;
            entry.lastUsedMs = QDateTime::currentMSecsSinceEpoch();
            changed = true;
        }
    }
    if (changed) {
        store(settings, entries);
    }
}

} // namespace speecher::VocabularySettingsCodec
