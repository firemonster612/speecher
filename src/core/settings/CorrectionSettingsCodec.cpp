#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/VocabularySettingsCodec.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <iterator>

namespace speecher::CorrectionSettingsCodec {
namespace {

struct PendingEvidence {
    QString original;
    QString corrected;
    QString applicationId;
    int count = 0;
    qint64 firstObservedAtMs = 0;
    qint64 lastObservedAtMs = 0;
    double confidence = 0.0;
};

QList<PendingEvidence> loadPending(const QSettings &settings)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(SettingsKeys::CorrectionEvidence, QByteArray()).toByteArray());
    QList<PendingEvidence> evidence;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        PendingEvidence item;
        item.original = object.value(QStringLiteral("original")).toString();
        item.corrected = object.value(QStringLiteral("corrected")).toString();
        item.applicationId = object.value(QStringLiteral("applicationId")).toString();
        item.count = object.value(QStringLiteral("count")).toInt();
        item.firstObservedAtMs = qint64(object.value(QStringLiteral("firstObservedAtMs")).toDouble());
        item.lastObservedAtMs = qint64(object.value(QStringLiteral("lastObservedAtMs")).toDouble());
        item.confidence = object.value(QStringLiteral("confidence")).toDouble();
        if (!item.original.isEmpty() && !item.corrected.isEmpty() && item.count > 0) {
            evidence.append(item);
        }
    }
    return evidence;
}

void storePending(QSettings &settings, const QList<PendingEvidence> &evidence)
{
    QJsonArray array;
    for (const PendingEvidence &item : evidence) {
        array.append(QJsonObject{
            {QStringLiteral("original"), item.original},
            {QStringLiteral("corrected"), item.corrected},
            {QStringLiteral("applicationId"), item.applicationId},
            {QStringLiteral("count"), item.count},
            {QStringLiteral("firstObservedAtMs"), double(item.firstObservedAtMs)},
            {QStringLiteral("lastObservedAtMs"), double(item.lastObservedAtMs)},
            {QStringLiteral("confidence"), item.confidence},
        });
    }
    settings.setValue(SettingsKeys::CorrectionEvidence,
                      QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool sameText(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

} // namespace

bool learningEnabled(const QSettings &settings)
{
    return settings.value(SettingsKeys::CorrectionLearningEnabled, true).toBool();
}

void storeLearningEnabled(QSettings &settings, bool value)
{
    settings.setValue(SettingsKeys::CorrectionLearningEnabled, value);
}

bool recordEvidence(QSettings &settings,
                    const CorrectionEvidence &evidence,
                    const QString &applicationId)
{
    const QString original = evidence.original.trimmed();
    const QString corrected = evidence.corrected.trimmed();
    const QString application = applicationId.trimmed();
    if (original.isEmpty() || corrected.isEmpty() || application.isEmpty()
        || original == corrected
        || evidence.confidence < 0.65) {
        return false;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<VocabularyEntry> entries = VocabularySettingsCodec::load(settings);
    const auto observed = [&](VocabularyEntry &correction) {
        ++correction.evidenceCount;
        correction.lastObservedAtMs = now;
        correction.confidence = std::max(correction.confidence, evidence.confidence);
        VocabularySettingsCodec::store(settings, entries);
    };
    for (VocabularyEntry &correction : entries) {
        const bool sameOriginal = correction.isLearned() && sameText(correction.term, original);
        const bool sameScope = correction.apps.isEmpty()
            || std::any_of(correction.apps.cbegin(), correction.apps.cend(),
                           [&application](const QString &app) { return sameText(app, application); });
        if (!sameOriginal || !sameScope) {
            continue;
        }
        if (!sameText(correction.replacement, corrected)) {
            return false;
        }
        observed(correction);
        return true;
    }

    // The same correction learned in another app is one for every app.
    for (VocabularyEntry &correction : entries) {
        if (correction.isLearned() && sameText(correction.term, original)
            && sameText(correction.replacement, corrected)) {
            correction.apps.clear();
            observed(correction);
            return true;
        }
    }

    QList<PendingEvidence> pending = loadPending(settings);
    auto matching = std::find_if(pending.begin(), pending.end(), [&](const PendingEvidence &item) {
        return sameText(item.original, original)
            && sameText(item.corrected, corrected)
            && sameText(item.applicationId, application);
    });
    if (matching == pending.end()) {
        matching = std::find_if(pending.begin(), pending.end(), [&](const PendingEvidence &item) {
            return sameText(item.original, original)
                && sameText(item.corrected, corrected);
        });
        if (matching != pending.end()) {
            matching->applicationId.clear();
        }
    }
    if (matching == pending.end()) {
        pending.append({original, corrected, application, 1, now, now, evidence.confidence});
        matching = std::prev(pending.end());
    } else {
        ++matching->count;
        matching->lastObservedAtMs = now;
        matching->confidence = std::max(matching->confidence, evidence.confidence);
    }

    if (evidence.confidence < 0.9 && matching->count < 2) {
        storePending(settings, pending);
        return false;
    }

    const PendingEvidence activated = *matching;
    pending.erase(matching);
    VocabularyEntry learned;
    learned.term = activated.original;
    learned.replacement = activated.corrected;
    learned.source = QStringLiteral("learned");
    if (!activated.applicationId.isEmpty()) {
        learned.apps = {activated.applicationId};
    }
    learned.createdAtMs = activated.firstObservedAtMs;
    learned.confidence = activated.confidence;
    learned.evidenceCount = activated.count;
    learned.lastObservedAtMs = activated.lastObservedAtMs;
    // Newest first among the replacements, as the page lists them.
    entries.prepend(learned);
    storePending(settings, pending);
    VocabularySettingsCodec::store(settings, entries);
    return true;
}

} // namespace speecher::CorrectionSettingsCodec
