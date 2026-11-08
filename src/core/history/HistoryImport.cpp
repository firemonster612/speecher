#include "core/history/HistoryImport.h"

#include "core/history/HistoryStore.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {
namespace {

// The stats both formats share, under the insights log's names.
std::optional<DictationRecord> recordFrom(const QJsonObject &object)
{
    const QDateTime finishedAt =
        QDateTime::fromString(object.value(QLatin1String("finishedAt")).toString(), Qt::ISODate);
    if (!finishedAt.isValid()) {
        return std::nullopt;
    }
    DictationRecord record;
    record.finishedAt = finishedAt;
    record.audioMs = object.value(QLatin1String("audioMs")).toInt();
    record.words = object.value(QLatin1String("words")).toInt();
    record.appName = object.value(QLatin1String("app")).toString();
    record.profile = writingProfileFromName(object.value(QLatin1String("profile")).toString());
    record.profileName = object.value(QLatin1String("profileName")).toString();
    record.speechProviders = object.value(QLatin1String("speechProviders")).toVariant().toStringList();
    record.refinementProviders = object.value(QLatin1String("refinementProviders")).toVariant().toStringList();
    record.applicationId = object.value(QLatin1String("appId")).toString();
    return record;
}

} // namespace

std::optional<QList<HistoryEntry>> readInsightsLog(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QList<HistoryEntry> entries;
    int lineNumber = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        ++lineNumber;
        if (line.isEmpty()) {
            continue;
        }
        const std::optional<DictationRecord> record = recordFrom(QJsonDocument::fromJson(line).object());
        if (!record) {
            qWarning().noquote() << "insights log skipped unreadable line" << lineNumber << "path=" + path;
            continue;
        }
        HistoryEntry entry;
        entry.stats = *record;
        // The log never kept an app's id, and History keeps one only with text.
        entry.stats.applicationId.clear();
        entries.append(entry);
    }
    return entries;
}

bool importInsightsLog(HistoryStore &store, const QString &path)
{
    if (!QFile::exists(path)) {
        return true;
    }
    const std::optional<QList<HistoryEntry>> entries = readInsightsLog(path);
    if (!entries || !store.addAll(*entries)) {
        qWarning().noquote() << "insights log not imported into History; trying again next start path=" + path;
        return false;
    }
    const QString imported = path + QStringLiteral(".imported");
    QFile::remove(imported);
    // A log left in place would be imported again, twice the dictations.
    if (!QFile::rename(path, imported) && !QFile::remove(path)) {
        qWarning().noquote() << "insights log imported but could not be renamed path=" + path;
    }
    qInfo().noquote() << "insights log imported into History dictations=" << entries->size();
    return true;
}

std::optional<QList<HistoryEntry>> readHistorySeed(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return std::nullopt;
    }
    QList<HistoryEntry> entries;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const std::optional<DictationRecord> record = recordFrom(object);
        if (!record) {
            continue;
        }
        HistoryEntry entry;
        entry.stats = *record;
        entry.kind = historyKindFromName(object.value(QLatin1String("kind")).toString()).value_or(HistoryKind::Dictation);
        entry.outcome = historyOutcomeFromName(object.value(QLatin1String("outcome")).toString())
                            .value_or(entry.kind == HistoryKind::File ? HistoryOutcome::Done : HistoryOutcome::Delivered);
        entry.title = object.value(QLatin1String("title")).toString();
        entry.sourcePath = object.value(QLatin1String("sourcePath")).toString();
        if (object.contains(QLatin1String("text"))) {
            entry.text = object.value(QLatin1String("text")).toString();
            entry.rawText = object.value(QLatin1String("rawText")).toString(*entry.text);
            entry.kept = HistoryKeep::Text;
        }
        entries.append(entry);
    }
    return entries;
}

} // namespace speecher
