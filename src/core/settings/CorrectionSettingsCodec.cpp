#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

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

QList<LearnedCorrection> load(const QSettings &settings)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(SettingsKeys::LearnedCorrections, QByteArray()).toByteArray());
    QList<LearnedCorrection> corrections;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        LearnedCorrection correction;
        correction.id = object.value(QStringLiteral("id")).toString();
        correction.original = object.value(QStringLiteral("original")).toString();
        correction.corrected = object.value(QStringLiteral("corrected")).toString();
        correction.applicationId = object.value(QStringLiteral("applicationId")).toString();
        correction.createdAtMs = qint64(object.value(QStringLiteral("createdAtMs")).toDouble());
        correction.confidence = object.value(QStringLiteral("confidence")).toDouble();
        correction.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        correction.evidenceCount = object.value(QStringLiteral("evidenceCount")).toInt(1);
        correction.lastObservedAtMs = qint64(
            object.value(QStringLiteral("lastObservedAtMs")).toDouble(correction.createdAtMs));
        if (!correction.id.isEmpty()
            && !correction.original.trimmed().isEmpty()
            && !correction.corrected.trimmed().isEmpty()) {
            corrections.append(correction);
        }
    }
    return corrections;
}

static void storeLearnedCorrections(QSettings *settings, const QList<LearnedCorrection> &corrections)
{
    QJsonArray array;
    for (const LearnedCorrection &correction : corrections) {
        array.append(QJsonObject{
            {QStringLiteral("id"), correction.id},
            {QStringLiteral("original"), correction.original},
            {QStringLiteral("corrected"), correction.corrected},
            {QStringLiteral("applicationId"), correction.applicationId},
            {QStringLiteral("createdAtMs"), double(correction.createdAtMs)},
            {QStringLiteral("confidence"), correction.confidence},
            {QStringLiteral("enabled"), correction.enabled},
            {QStringLiteral("evidenceCount"), correction.evidenceCount},
            {QStringLiteral("lastObservedAtMs"), double(correction.lastObservedAtMs)},
        });
    }
    settings->setValue(
        SettingsKeys::LearnedCorrections,
        QJsonDocument(array).toJson(QJsonDocument::Compact));
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
    QList<LearnedCorrection> corrections = load(settings);
    for (LearnedCorrection &correction : corrections) {
        const bool sameOriginal = sameText(correction.original, original);
        const bool sameScope = sameText(correction.applicationId, application)
            || correction.applicationId.isEmpty();
        if (!sameOriginal || !sameScope) {
            continue;
        }
        if (!sameText(correction.corrected, corrected)) {
            return false;
        }
        ++correction.evidenceCount;
        correction.lastObservedAtMs = now;
        correction.confidence = std::max(correction.confidence, evidence.confidence);
        storeLearnedCorrections(&settings, corrections);
        return true;
    }

    for (LearnedCorrection &correction : corrections) {
        if (sameText(correction.original, original)
            && sameText(correction.corrected, corrected)
            && !sameText(correction.applicationId, application)) {
            correction.applicationId.clear();
            ++correction.evidenceCount;
            correction.lastObservedAtMs = now;
            correction.confidence = std::max(correction.confidence, evidence.confidence);
            storeLearnedCorrections(&settings, corrections);
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
    corrections.prepend({
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        activated.original,
        activated.corrected,
        activated.applicationId,
        activated.firstObservedAtMs,
        activated.confidence,
        true,
        activated.count,
        activated.lastObservedAtMs,
    });
    storePending(settings, pending);
    storeLearnedCorrections(&settings, corrections);
    return true;
}

void store(QSettings &settings, const QList<LearnedCorrection> &corrections)
{
    storeLearnedCorrections(&settings, corrections);
}

void setEnabled(QSettings &settings, const QString &id, bool enabled)
{
    QList<LearnedCorrection> corrections = load(settings);
    for (LearnedCorrection &correction : corrections) {
        if (correction.id == id) {
            correction.enabled = enabled;
            storeLearnedCorrections(&settings, corrections);
            return;
        }
    }
}

void remove(QSettings &settings, const QString &id)
{
    QList<LearnedCorrection> corrections = load(settings);
    corrections.removeIf([&id](const LearnedCorrection &correction) {
        return correction.id == id;
    });
    storeLearnedCorrections(&settings, corrections);
}

} // namespace speecher::CorrectionSettingsCodec
