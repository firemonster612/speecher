#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/SettingsKeys.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace speecher::CorrectionSettingsCodec {

bool learningEnabled(const QSettings &settings)
{
    return settings.value(SettingsKeys::CorrectionLearningEnabled, false).toBool();
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
        });
    }
    settings->setValue(
        SettingsKeys::LearnedCorrections,
        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void store(QSettings &settings, const QList<LearnedCorrection> &corrections)
{
    storeLearnedCorrections(&settings, corrections);
}

bool add(QSettings &settings, const QString &original,
                                         const QString &corrected,
                                         const QString &applicationId,
                                         double confidence)
{
    const QString from = original.trimmed();
    const QString to = corrected.trimmed();
    if (from.isEmpty() || to.isEmpty() || from == to || from.size() > 500 || to.size() > 500) {
        return false;
    }

    QList<LearnedCorrection> corrections = load(settings);
    for (LearnedCorrection &correction : corrections) {
        if (correction.original.compare(from, Qt::CaseInsensitive) == 0
            && correction.applicationId.compare(applicationId, Qt::CaseInsensitive) == 0) {
            correction.corrected = to;
            correction.confidence = qBound(0.0, confidence, 1.0);
            correction.createdAtMs = QDateTime::currentMSecsSinceEpoch();
            correction.enabled = true;
            storeLearnedCorrections(&settings, corrections);
            return true;
        }
    }

    corrections.prepend({
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        from,
        to,
        applicationId.trimmed(),
        QDateTime::currentMSecsSinceEpoch(),
        qBound(0.0, confidence, 1.0),
        true,
    });
    storeLearnedCorrections(&settings, corrections);
    return true;
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
