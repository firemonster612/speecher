#include "core/history/HistoryImport.h"

#include "core/history/HistoryStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace speecher {
namespace {

// How History marks the insights log imported (HistoryStore::importAll).
const QString insightsLogSource = QStringLiteral("insights.jsonl");

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
    // The dictations and the mark that they came in commit together, so a
    // log a crash or a refused rename left in place is never added twice.
    if (!store.imported(insightsLogSource)) {
        const std::optional<QList<HistoryEntry>> entries = readInsightsLog(path);
        if (!entries || !store.importAll(insightsLogSource, *entries)) {
            qWarning().noquote() << "insights log not imported into History; trying again next start path=" + path;
            return false;
        }
        qInfo().noquote() << "insights log imported into History dictations=" << entries->size();
    }
    const QString imported = path + QStringLiteral(".imported");
    QFile::remove(imported);
    if (!QFile::rename(path, imported) && !QFile::remove(path)) {
        qWarning().noquote() << "insights log imported but could not be renamed; trying again next start path=" + path;
    }
    return true;
}

std::optional<QList<HistorySeedEntry>> readHistorySeed(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return std::nullopt;
    }
    const QDir folder = QFileInfo(path).absoluteDir();
    QList<HistorySeedEntry> entries;
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
        // A file's audio and saved transcript, and a Recording's file, beside
        // the seed or where they say.
        const auto file = [&object, &folder](const char *key) {
            const QString path = object.value(QLatin1String(key)).toString();
            return path.isEmpty() ? QString() : folder.absoluteFilePath(path);
        };
        entry.sourcePath = file("sourcePath");
        entry.transcriptPath = file("transcriptPath");
        if (object.contains(QLatin1String("text"))) {
            entry.text = object.value(QLatin1String("text")).toString();
            entry.rawText = object.value(QLatin1String("rawText")).toString(*entry.text);
            entry.kept = HistoryKeep::Text;
        }
        QList<HistoryLine> lines;
        if (entry.kind == HistoryKind::Recording) {
            entry.kept = HistoryKeep::Text;
            entry.startedAt = QDateTime::fromString(object.value(QLatin1String("startedAt")).toString(), Qt::ISODate);
            entry.end = recordingEndFromJson(object.value(QLatin1String("ending")).toObject());
            for (const QJsonValue &value : object.value(QLatin1String("lines")).toArray()) {
                const QJsonObject line = value.toObject();
                lines.append({line.value(QLatin1String("atMs")).toInteger(), line.value(QLatin1String("speaker")).toString(),
                              line.value(QLatin1String("text")).toString()});
            }
        }
        entries.append({entry, lines});
    }
    return entries;
}

void addHistorySeed(HistoryStore &store, QList<HistorySeedEntry> seed)
{
    std::stable_sort(seed.begin(), seed.end(), [](const HistorySeedEntry &a, const HistorySeedEntry &b) {
        return a.entry.stats.finishedAt < b.entry.stats.finishedAt;
    });
    for (const auto &[entry, lines] : std::as_const(seed)) {
        const qint64 id = store.add(entry);
        if (entry.kind != HistoryKind::Recording || id == 0) {
            continue;
        }
        for (const HistoryLine &line : lines) {
            store.appendLine(id, line);
        }
        store.finishRecording(id, entry.stats.finishedAt, entry.stats.audioMs, entry.end, {});
    }
}

} // namespace speecher
