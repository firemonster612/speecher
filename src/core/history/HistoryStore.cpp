#include "core/history/HistoryStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>

namespace speecher {
namespace {

constexpr int schemaVersion = 2;
// Search terms shorter than a trigram can't use the index; alone, they scan
// only this many of the newest entries.
constexpr int shortSearchWindow = 2000;
constexpr qint64 msPerDay = 24LL * 60 * 60 * 1000;
const QString memoryPath = QStringLiteral(":memory:");

// The schema of version 1. The search index is made apart, at each open,
// only where SQLite has the trigram tokenizer (prepareSearchIndex).
const char *const schemaStatements[] = {
    R"(CREATE TABLE entries (
         id INTEGER PRIMARY KEY,
         kind TEXT NOT NULL,
         started_at INTEGER,
         finished_at INTEGER,
         utc_offset_s INTEGER NOT NULL,
         audio_ms INTEGER NOT NULL DEFAULT 0,
         words INTEGER NOT NULL DEFAULT 0,
         app_name TEXT,
         app_id TEXT,
         profile TEXT,
         profile_name TEXT,
         speech TEXT,
         refinement TEXT,
         outcome TEXT NOT NULL,
         title TEXT,
         text TEXT,
         raw_text TEXT,
         audio_path TEXT,
         source_path TEXT,
         transcript_path TEXT,
         transcript_mtime INTEGER,
         kept TEXT NOT NULL,
         deleted_at INTEGER))",
    "CREATE INDEX entries_time ON entries(finished_at DESC, id DESC)",
    "CREATE INDEX entries_app ON entries(app_name)",
    // A Recording's lines, which Recordings fills.
    R"(CREATE TABLE lines (
         id INTEGER PRIMARY KEY,
         entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
         at_ms INTEGER NOT NULL,
         speaker TEXT NOT NULL,
         text TEXT NOT NULL))",
    "CREATE INDEX lines_entry ON lines(entry_id)",
};
// What takes version n to n + 1, from version 1.
const char *const schemaUpgrades[] = {
    // A Recording's RecordingEnd, as recordingEndJson.
    "ALTER TABLE entries ADD COLUMN ending TEXT",
};

// Columns are named with their table, since a search joins the index, whose
// columns share their names.
// What every question leaves out: entries waiting on an Undo.
const QString visible = QStringLiteral("entries.deleted_at IS NULL");
// What the list shows: entries with words to find. Recordings keep theirs.
const QString listed = visible + QStringLiteral(" AND (entries.text IS NOT NULL OR entries.kind = 'recording')");
// Kinds that clearing and text retention apply to.
const QString clearedKinds = QStringLiteral("entries.kind IN ('dictation', 'edit', 'file')");
const QString columns = QStringLiteral(
    "entries.id, entries.kind, entries.started_at, entries.finished_at, entries.utc_offset_s, entries.audio_ms,"
    " entries.words, entries.app_name, entries.app_id, entries.profile, entries.profile_name, entries.speech,"
    " entries.refinement, entries.outcome, entries.title, entries.text, entries.raw_text, entries.audio_path,"
    " entries.source_path, entries.transcript_path, entries.kept, entries.ending");

QVariant optionalText(const std::optional<QString> &text)
{
    return text ? QVariant(*text) : QVariant(QMetaType(QMetaType::QString));
}

QVariant nonEmpty(const QString &text)
{
    return text.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(text);
}

QVariant listJson(const QStringList &list)
{
    return list.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                          : QVariant(QString::fromUtf8(
                                QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact)));
}

QStringList listFromJson(const QVariant &json)
{
    return QJsonDocument::fromJson(json.toString().toUtf8()).toVariant().toStringList();
}

// " AND entries.id NOT IN (…)" for entries a change must leave alone; empty
// for none.
QString except(const QList<qint64> &ids)
{
    QStringList listed;
    for (const qint64 id : ids) {
        listed.append(QString::number(id));
    }
    return ids.isEmpty() ? QString() : QStringLiteral(" AND entries.id NOT IN (%1)").arg(listed.join(QLatin1Char(',')));
}

// A Recording's text, its lines' words a line each without times or
// speakers, so a search finds what was said, and how many words.
std::pair<QString, qsizetype> textOf(const QList<HistoryLine> &lines)
{
    QStringList said;
    qsizetype words = 0;
    for (const HistoryLine &line : lines) {
        said.append(line.text);
        words += line.text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
    }
    return {said.join(QLatin1Char('\n')), words};
}

// When a file last changed, as transcript_mtime keeps it; null without one.
QVariant changedAt(const QString &path)
{
    const QFileInfo file(path);
    return file.exists() ? QVariant(file.lastModified().toMSecsSinceEpoch()) : QVariant(QMetaType(QMetaType::LongLong));
}

QVariant msOf(const QDateTime &time)
{
    return time.isValid() ? QVariant(time.toMSecsSinceEpoch()) : QVariant(QMetaType(QMetaType::LongLong));
}

// The local wall-clock time an entry was made at, wherever this computer is
// now, as the insights log kept it.
QDateTime localTime(const QVariant &ms, int offsetSeconds)
{
    if (ms.isNull()) {
        return {};
    }
    const QDateTime wall = QDateTime::fromMSecsSinceEpoch(ms.toLongLong() + offsetSeconds * 1000LL, QTimeZone::UTC);
    return QDateTime(wall.date(), wall.time());
}

// The entry as kept may hold no more than `kept` allows.
HistoryEntry withinKept(HistoryEntry entry)
{
    if (entry.kept < HistoryKeep::Text) {
        entry.text.reset();
        entry.rawText.reset();
        entry.stats.applicationId.clear();
    }
    if (entry.kept < HistoryKeep::Audio) {
        entry.audioPath.clear();
    }
    return entry;
}

HistoryEntry entryFrom(const QSqlQuery &row)
{
    HistoryEntry entry;
    const int offset = row.value(4).toInt();
    entry.id = row.value(0).toLongLong();
    entry.kind = historyKindFromName(row.value(1).toString()).value_or(HistoryKind::Dictation);
    entry.startedAt = localTime(row.value(2), offset);
    entry.stats.finishedAt = localTime(row.value(3), offset);
    entry.stats.audioMs = row.value(5).toInt();
    entry.stats.words = row.value(6).toInt();
    entry.stats.appName = row.value(7).toString();
    entry.stats.applicationId = row.value(8).toString();
    entry.stats.profile = row.value(9).toString();
    entry.stats.profileName = row.value(10).toString();
    entry.stats.speechProviders = listFromJson(row.value(11));
    entry.stats.refinementProviders = listFromJson(row.value(12));
    entry.outcome = historyOutcomeFromName(row.value(13).toString()).value_or(HistoryOutcome::Delivered);
    entry.title = row.value(14).toString();
    if (!row.value(15).isNull()) {
        entry.text = row.value(15).toString();
    }
    if (!row.value(16).isNull()) {
        entry.rawText = row.value(16).toString();
    }
    entry.audioPath = row.value(17).toString();
    entry.sourcePath = row.value(18).toString();
    entry.transcriptPath = row.value(19).toString();
    entry.kept = historyKeepFromName(row.value(20).toString()).value_or(HistoryKeep::Stats);
    entry.end = recordingEndFromJson(QJsonDocument::fromJson(row.value(21).toString().toUtf8()).object());
    return entry;
}

// A term as an FTS5 string, which the trigram tokenizer matches as a substring.
QString ftsString(const QString &term)
{
    return QLatin1Char('"') + QString(term).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}

// A WHERE clause and its values, built up a condition at a time, over the
// entries alone or, for a search the index answers, joined to it.
struct Where {
    QStringList conditions;
    QVariantList values;
    bool joinsIndex = false;

    void add(const QString &condition, const QVariantList &conditionValues = {})
    {
        conditions.append(condition);
        values.append(conditionValues);
    }
    QString sql() const { return conditions.join(QStringLiteral(" AND ")); }
    QString from() const
    {
        return joinsIndex ? QStringLiteral("entries_fts JOIN entries ON entries.id = entries_fts.rowid")
                          : QStringLiteral("entries");
    }
    // Newest first. Ordered by the index's rowid where it is joined, so it
    // hands over its matches newest first and stops at the page's end.
    QString newestFirst() const
    {
        return joinsIndex ? QStringLiteral("entries_fts.rowid DESC") : QStringLiteral("entries.id DESC");
    }
};

QString kindList(const QList<HistoryKind> &kinds)
{
    QStringList names;
    for (const HistoryKind kind : kinds) {
        names.append(QLatin1Char('\'') + historyKindName(kind) + QLatin1Char('\''));
    }
    return names.join(QStringLiteral(", "));
}

void addRange(Where &where, const QDateTime &from, const QDateTime &to)
{
    if (from.isValid()) {
        where.add(QStringLiteral("entries.finished_at >= ?"), {from.toMSecsSinceEpoch()});
    }
    if (to.isValid()) {
        where.add(QStringLiteral("entries.finished_at < ?"), {to.toMSecsSinceEpoch()});
    }
}

// What a query asks of the listed entries, on every page.
Where searchWhere(const HistoryQuery &query, bool trigram)
{
    Where where;
    const QStringList terms = historySearchTerms(query.text);
    QStringList indexed;
    QStringList scanned;
    for (const QString &term : terms) {
        (trigram && term.size() >= 3 ? indexed : scanned).append(term);
    }
    if (!indexed.isEmpty()) {
        for (QString &term : indexed) {
            term = ftsString(term);
        }
        where.joinsIndex = true;
        where.add(QStringLiteral("entries_fts MATCH ?"), {indexed.join(QLatin1Char(' '))});
    } else if (trigram && !scanned.isEmpty()) {
        where.add(QStringLiteral("entries.id >= (SELECT min(id) FROM (SELECT entries.id FROM entries WHERE %1"
                                 " ORDER BY entries.id DESC LIMIT %2))")
                      .arg(listed, QString::number(shortSearchWindow)));
    }
    where.add(listed);
    for (const QString &term : std::as_const(scanned)) {
        where.add(QStringLiteral("instr(lower(coalesce(entries.text, '') || char(10) || coalesce(entries.raw_text, '')"
                                 " || char(10) || coalesce(entries.app_name, '') || char(10)"
                                 " || coalesce(entries.title, '')), ?) > 0"),
                  {term});
    }
    if (!query.kinds.isEmpty()) {
        where.add(QStringLiteral("entries.kind IN (%1)").arg(kindList(query.kinds)));
    }
    if (!query.app.isEmpty()) {
        where.add(QStringLiteral("entries.app_name = ?"), {query.app});
    }
    if (!query.profile.isEmpty()) {
        where.add(QStringLiteral("entries.profile = ?"), {query.profile});
    }
    addRange(where, query.from, query.to);
    return where;
}

} // namespace

HistoryStore::HistoryStore(const QString &databasePath, const QString &audioFolder, QObject *parent)
    : QObject(parent)
    , m_connection(QStringLiteral("history-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_audioFolder(audioFolder)
{
    const bool inMemory = databasePath == memoryPath;
    if (!inMemory && !QFile::exists(databasePath)) {
        // Created closed to other users before SQLite writes a byte; its
        // journal files take the same permissions.
        QDir().mkpath(QFileInfo(databasePath).absolutePath());
        QFile file(databasePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        }
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(databasePath);
    if (!db.isValid() || !db.open()) {
        qWarning().noquote() << "history store could not open path=" + databasePath
                             << "error=" + db.lastError().text();
        return;
    }
    m_open = true;
    if (!inMemory) {
        exec(QStringLiteral("PRAGMA journal_mode = WAL"));
        exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    }
    // Deleted text is overwritten in the file, not left in free pages.
    exec(QStringLiteral("PRAGMA secure_delete = ON"));
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    createSchema();
    prepareSearchIndex();
    purgeDeleted();
    qInfo().noquote() << "history store opened path=" + databasePath
                      << "search=" + QString(m_trigram ? QStringLiteral("trigram") : QStringLiteral("instr"));
}

HistoryStore::~HistoryStore()
{
    purgeDeleted();
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

bool HistoryStore::isOpen() const
{
    return m_open;
}

bool HistoryStore::searchesByTrigram() const
{
    return m_trigram;
}

QString HistoryStore::audioFile(const HistoryEntry &entry) const
{
    if (entry.audioPath.isEmpty()) {
        return {};
    }
    return QDir::isAbsolutePath(entry.audioPath) ? entry.audioPath : QDir(m_audioFolder).filePath(entry.audioPath);
}

bool HistoryStore::exec(const QString &sql) const
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (!query.exec(sql)) {
        qWarning().noquote() << "history store statement failed:" << sql.left(80) << query.lastError().text();
        return false;
    }
    return true;
}

void HistoryStore::createSchema()
{
    QSqlQuery version(QSqlDatabase::database(m_connection));
    version.exec(QStringLiteral("PRAGMA user_version"));
    const int stored = version.next() ? version.value(0).toInt() : 0;
    if (stored >= schemaVersion) {
        if (stored > schemaVersion) {
            qWarning().noquote() << "history store was written by a newer Speecher, version" << stored;
        }
        return;
    }
    QStringList statements;
    if (stored == 0) {
        for (const char *statement : schemaStatements) {
            statements.append(QString::fromUtf8(statement));
        }
    }
    for (int upgrade = std::max(stored, 1); upgrade < schemaVersion; ++upgrade) {
        statements.append(QString::fromUtf8(schemaUpgrades[upgrade - 1]));
    }
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    for (const QString &statement : std::as_const(statements)) {
        if (!exec(statement)) {
            db.rollback();
            m_open = false;
            return;
        }
    }
    exec(QStringLiteral("PRAGMA user_version = %1").arg(schemaVersion));
    db.commit();
}

// The index lives in the file, but the triggers that keep it in step are made
// for this connection only. An SQLite without the trigram tokenizer then
// never touches the index, which it couldn't open, and marks it stale for the
// next open that can.
void HistoryStore::prepareSearchIndex()
{
    if (!m_open) {
        return;
    }
    QSqlQuery probe(QSqlDatabase::database(m_connection));
    // SPEECHER_HISTORY_WITHOUT_TRIGRAM=1 stands in for an SQLite without it,
    // for tests of the scan.
    m_trigram = qEnvironmentVariableIntValue("SPEECHER_HISTORY_WITHOUT_TRIGRAM") != 1
        && probe.exec(QStringLiteral("CREATE VIRTUAL TABLE temp.trigram_probe USING fts5(x, tokenize = 'trigram')"));
    if (!m_trigram) {
        qInfo().noquote() << "history search scans without an index: SQLite has no FTS5 trigram tokenizer,"
                          << probe.lastError().text();
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS search_state (stale INTEGER)"));
        exec(QStringLiteral("DELETE FROM search_state"));
        exec(QStringLiteral("INSERT INTO search_state VALUES (1)"));
        return;
    }
    exec(QStringLiteral("DROP TABLE temp.trigram_probe"));
    bool created = false;
    {
        QSqlQuery existing(QSqlDatabase::database(m_connection));
        created = existing.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name = 'entries_fts'"))
            && !existing.next();
    }
    exec(QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(text, raw_text, app_name, title,"
                        " content = 'entries', content_rowid = 'id', tokenize = 'trigram')"));
    const QString fields = QStringLiteral("text, raw_text, app_name, title");
    exec(QStringLiteral("CREATE TEMP TRIGGER entries_fts_insert AFTER INSERT ON main.entries BEGIN"
                        " INSERT INTO entries_fts(rowid, %1) VALUES (new.id, new.text, new.raw_text, new.app_name, new.title);"
                        " END")
             .arg(fields));
    exec(QStringLiteral("CREATE TEMP TRIGGER entries_fts_delete AFTER DELETE ON main.entries BEGIN"
                        " INSERT INTO entries_fts(entries_fts, rowid, %1)"
                        " VALUES ('delete', old.id, old.text, old.raw_text, old.app_name, old.title);"
                        " END")
             .arg(fields));
    exec(QStringLiteral("CREATE TEMP TRIGGER entries_fts_update AFTER UPDATE OF %1 ON main.entries BEGIN"
                        " INSERT INTO entries_fts(entries_fts, rowid, %1)"
                        " VALUES ('delete', old.id, old.text, old.raw_text, old.app_name, old.title);"
                        " INSERT INTO entries_fts(rowid, %1) VALUES (new.id, new.text, new.raw_text, new.app_name, new.title);"
                        " END")
             .arg(fields));
    bool wasStale = false;
    {
        QSqlQuery stale(QSqlDatabase::database(m_connection));
        wasStale = stale.exec(QStringLiteral("SELECT stale FROM search_state")) && stale.next();
    }
    if (created || wasStale) {
        exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES ('rebuild')"));
        exec(QStringLiteral("DROP TABLE IF EXISTS search_state"));
    }
}

qint64 HistoryStore::add(const HistoryEntry &given)
{
    if (!m_open) {
        return 0;
    }
    const HistoryEntry entry = withinKept(given);
    QSqlQuery insert(QSqlDatabase::database(m_connection));
    insert.prepare(QStringLiteral(
        "INSERT INTO entries (kind, started_at, finished_at, utc_offset_s, audio_ms, words, app_name, app_id, profile,"
        " profile_name, speech, refinement, outcome, title, text, raw_text, audio_path, source_path, transcript_path,"
        " kept) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const DictationRecord &stats = entry.stats;
    for (const QVariant &value : QVariantList{
             historyKindName(entry.kind), msOf(entry.startedAt), msOf(stats.finishedAt),
             stats.finishedAt.isValid()   ? stats.finishedAt.offsetFromUtc()
             : entry.startedAt.isValid() ? entry.startedAt.offsetFromUtc()
                                         : 0,
             stats.audioMs, stats.words,
             nonEmpty(stats.appName), nonEmpty(stats.applicationId), nonEmpty(stats.profile), nonEmpty(stats.profileName),
             listJson(stats.speechProviders), listJson(stats.refinementProviders), historyOutcomeName(entry.outcome),
             nonEmpty(entry.title), optionalText(entry.text), optionalText(entry.rawText), nonEmpty(entry.audioPath),
             nonEmpty(entry.sourcePath), nonEmpty(entry.transcriptPath), historyKeepName(entry.kept)}) {
        insert.addBindValue(value);
    }
    if (!insert.exec()) {
        qWarning().noquote() << "history store could not add an entry:" << insert.lastError().text();
        return 0;
    }
    const bool counted = (entry.kind == HistoryKind::Dictation || entry.kind == HistoryKind::Edit)
        && (entry.outcome == HistoryOutcome::Delivered || entry.outcome == HistoryOutcome::Copied);
    if (counted && m_records) {
        m_records->append(stats);
        m_records->last().applicationId.clear();
    }
    emit changed();
    return insert.lastInsertId().toLongLong();
}

bool HistoryStore::addAll(const QList<HistoryEntry> &entries)
{
    if (!m_open) {
        return false;
    }
    // Oldest first, so the timeline's order, which is the order entries came
    // in, is also the order they finished in.
    QList<HistoryEntry> oldestFirst = entries;
    std::stable_sort(oldestFirst.begin(), oldestFirst.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        return a.stats.finishedAt < b.stats.finishedAt;
    });
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSignalBlocker quiet(this);
    db.transaction();
    for (const HistoryEntry &entry : std::as_const(oldestFirst)) {
        if (add(entry) == 0) {
            db.rollback();
            forgetRecords();
            return false;
        }
    }
    const bool committed = db.commit();
    forgetRecords();
    quiet.unblock();
    emit changed();
    return committed;
}

QList<HistoryHit> HistoryStore::search(const HistoryQuery &query) const
{
    QList<HistoryHit> hits;
    if (!m_open) {
        return hits;
    }
    Where where = searchWhere(query, m_trigram);
    if (query.beforeId > 0) {
        where.add(QStringLiteral("entries.id < ?"), {query.beforeId});
    }
    QSqlQuery select(QSqlDatabase::database(m_connection));
    select.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE %3 ORDER BY %4 LIMIT %5")
                       .arg(columns, where.from(), where.sql(), where.newestFirst(),
                            QString::number(qMax(0, query.limit))));
    for (const QVariant &value : std::as_const(where.values)) {
        select.addBindValue(value);
    }
    if (!select.exec()) {
        qWarning().noquote() << "history search failed:" << select.lastError().text();
        return hits;
    }
    const QStringList terms = historySearchTerms(query.text);
    while (select.next()) {
        HistoryEntry entry = entryFrom(select);
        const QString highlighted = highlightHistoryMatches(entry.text.value_or(QString()), terms);
        hits.append({std::move(entry), highlighted});
    }
    return hits;
}

int HistoryStore::count(const HistoryQuery &query) const
{
    const Where where = searchWhere(query, m_trigram);
    QSqlQuery select(QSqlDatabase::database(m_connection));
    select.prepare(QStringLiteral("SELECT count(*) FROM %1 WHERE %2").arg(where.from(), where.sql()));
    for (const QVariant &value : where.values) {
        select.addBindValue(value);
    }
    return m_open && select.exec() && select.next() ? select.value(0).toInt() : 0;
}

std::optional<HistoryEntry> HistoryStore::entry(qint64 id) const
{
    QSqlQuery select(QSqlDatabase::database(m_connection));
    select.prepare(QStringLiteral("SELECT %1 FROM entries WHERE id = ? AND %2").arg(columns, visible));
    select.addBindValue(id);
    if (!m_open || !select.exec() || !select.next()) {
        return std::nullopt;
    }
    return entryFrom(select);
}

QStringList HistoryStore::appNames() const
{
    QStringList names;
    QSqlQuery select(QSqlDatabase::database(m_connection));
    if (m_open
        && select.exec(QStringLiteral("SELECT app_name FROM entries WHERE %1 AND entries.app_name IS NOT NULL"
                                      " GROUP BY app_name ORDER BY count(*) DESC, app_name")
                           .arg(listed))) {
        while (select.next()) {
            names.append(select.value(0).toString());
        }
    }
    return names;
}

QStringList HistoryStore::profiles() const
{
    QStringList ids;
    QSqlQuery select(QSqlDatabase::database(m_connection));
    if (m_open
        && select.exec(QStringLiteral("SELECT profile FROM entries WHERE %1 AND entries.profile IS NOT NULL"
                                      " GROUP BY profile ORDER BY count(*) DESC, profile")
                           .arg(listed))) {
        while (select.next()) {
            ids.append(select.value(0).toString());
        }
    }
    return ids;
}

bool HistoryStore::appendLine(qint64 entryId, const HistoryLine &line)
{
    QSqlQuery insert(QSqlDatabase::database(m_connection));
    insert.prepare(QStringLiteral("INSERT INTO lines (entry_id, at_ms, speaker, text) VALUES (?, ?, ?, ?)"));
    for (const QVariant &value : QVariantList{entryId, line.atMs, line.speaker, line.text}) {
        insert.addBindValue(value);
    }
    if (!m_open || !insert.exec()) {
        qWarning().noquote() << "history could not keep a recording's line:" << insert.lastError().text();
        return false;
    }
    return true;
}

QList<HistoryLine> HistoryStore::lines(qint64 entryId) const
{
    QList<HistoryLine> found;
    QSqlQuery select(QSqlDatabase::database(m_connection));
    select.prepare(QStringLiteral("SELECT at_ms, speaker, text FROM lines WHERE entry_id = ? ORDER BY id"));
    select.addBindValue(entryId);
    if (!m_open || !select.exec()) {
        return found;
    }
    while (select.next()) {
        found.append({select.value(0).toLongLong(), select.value(1).toString(), select.value(2).toString()});
    }
    return found;
}

// Its file's last change is kept too, so a later edit to it can be taken in
// (syncRecordingFile).
bool HistoryStore::finishRecording(qint64 id, const QDateTime &finishedAt, qint64 audioMs, const RecordingEnd &end,
                                   const QString &audioPath)
{
    const auto [said, words] = textOf(lines(id));
    const std::optional<HistoryEntry> found = entry(id);
    QSqlQuery update(QSqlDatabase::database(m_connection));
    update.prepare(QStringLiteral("UPDATE entries SET finished_at = ?, audio_ms = ?, words = ?, outcome = ?, text = ?,"
                                  " audio_path = ?, kept = ?, ending = ?, transcript_mtime = ?"
                                  " WHERE id = ? AND kind = 'recording'"));
    const HistoryOutcome outcome = end.missedSpeech() ? HistoryOutcome::Missed : HistoryOutcome::Complete;
    for (const QVariant &value :
         QVariantList{finishedAt.toMSecsSinceEpoch(), audioMs, words, historyOutcomeName(outcome), said,
                      nonEmpty(audioPath), historyKeepName(audioPath.isEmpty() ? HistoryKeep::Text : HistoryKeep::Audio),
                      QString::fromUtf8(QJsonDocument(recordingEndJson(end)).toJson(QJsonDocument::Compact)),
                      changedAt(found ? found->transcriptPath : QString()), id}) {
        update.addBindValue(value);
    }
    if (!m_open || !update.exec() || update.numRowsAffected() == 0) {
        qWarning().noquote() << "history could not finish a recording:" << update.lastError().text();
        return false;
    }
    emit changed();
    return true;
}

bool HistoryStore::syncRecordingFile(qint64 id)
{
    QString path;
    QVariant stored;
    {
        QSqlQuery select(QSqlDatabase::database(m_connection));
        select.prepare(QStringLiteral("SELECT transcript_path, transcript_mtime FROM entries"
                                      " WHERE id = ? AND kind = 'recording' AND finished_at IS NOT NULL"));
        select.addBindValue(id);
        if (!m_open || !select.exec() || !select.next()) {
            return true;
        }
        path = select.value(0).toString();
        stored = select.value(1);
    }
    const QVariant now = changedAt(path);
    if (now.isNull() || stored == now) {
        return true;
    }
    const auto keepChangedAt = [this, id, &now] {
        QSqlQuery update(QSqlDatabase::database(m_connection));
        update.prepare(QStringLiteral("UPDATE entries SET transcript_mtime = ? WHERE id = ?"));
        update.addBindValue(now);
        update.addBindValue(id);
        return update.exec();
    };
    // Finished before the store kept it: in step as far as anyone knows.
    if (stored.isNull()) {
        return keepChangedAt();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    static const QRegularExpression lineFormat(QStringLiteral(R"(^\[(\d+):(\d{2}):(\d{2})\] ([^:]+): (.*)$)"));
    QList<HistoryLine> read;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QRegularExpressionMatch match = lineFormat.match(line);
        if (!match.hasMatch()) {
            return false;
        }
        const qint64 seconds = (match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 60
            + match.captured(3).toLongLong();
        read.append({seconds * 1000, match.captured(4), match.captured(5)});
    }
    const auto [said, words] = textOf(read);
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    QSqlQuery change(db);
    change.prepare(QStringLiteral("DELETE FROM lines WHERE entry_id = ?"));
    change.addBindValue(id);
    bool written = change.exec();
    for (const HistoryLine &line : std::as_const(read)) {
        written = written && appendLine(id, line);
    }
    change.prepare(QStringLiteral("UPDATE entries SET text = ?, words = ? WHERE id = ?"));
    for (const QVariant &value : QVariantList{said, words, id}) {
        change.addBindValue(value);
    }
    if (!written || !change.exec() || !keepChangedAt() || !db.commit()) {
        db.rollback();
        return false;
    }
    emit changed();
    return true;
}

void HistoryStore::finishUnfinishedRecordings()
{
    QList<std::pair<qint64, QDateTime>> unfinished;
    {
        QSqlQuery select(QSqlDatabase::database(m_connection));
        if (!m_open
            || !select.exec(QStringLiteral("SELECT id, started_at, transcript_path FROM entries"
                                           " WHERE kind = 'recording' AND finished_at IS NULL"))) {
            return;
        }
        while (select.next()) {
            const QFileInfo file(select.value(2).toString());
            unfinished.append({select.value(0).toLongLong(),
                               file.exists() ? file.lastModified()
                                             : QDateTime::fromMSecsSinceEpoch(select.value(1).toLongLong())});
        }
    }
    for (const auto &[id, finishedAt] : std::as_const(unfinished)) {
        const std::optional<HistoryEntry> found = entry(id);
        const qint64 audioMs = found && found->startedAt.isValid() ? found->startedAt.msecsTo(finishedAt) : 0;
        RecordingEnd end;
        end.reason = RecordingEnd::Reason::Interrupted;
        finishRecording(id, finishedAt, qMax<qint64>(0, audioMs), end, QString());
    }
}

bool HistoryStore::setText(qint64 id, const QString &text)
{
    QSqlQuery update(QSqlDatabase::database(m_connection));
    update.prepare(QStringLiteral("UPDATE entries SET text = ? WHERE id = ? AND text IS NOT NULL AND ")
                   + clearedKinds);
    update.addBindValue(text);
    update.addBindValue(id);
    if (!m_open || !update.exec() || update.numRowsAffected() == 0) {
        return false;
    }
    emit changed();
    return true;
}

bool HistoryStore::remove(qint64 id)
{
    QSqlQuery remove(QSqlDatabase::database(m_connection));
    remove.prepare(QStringLiteral("DELETE FROM entries WHERE id = ?"));
    remove.addBindValue(id);
    if (!m_open || !remove.exec() || remove.numRowsAffected() == 0) {
        return false;
    }
    forgetRecords();
    emit changed();
    return true;
}

bool HistoryStore::markDeleted(qint64 id)
{
    QSqlQuery update(QSqlDatabase::database(m_connection));
    update.prepare(QStringLiteral("UPDATE entries SET deleted_at = ? WHERE id = ? AND deleted_at IS NULL"));
    update.addBindValue(QDateTime::currentMSecsSinceEpoch());
    update.addBindValue(id);
    if (!m_open || !update.exec() || update.numRowsAffected() == 0) {
        return false;
    }
    forgetRecords();
    emit changed();
    return true;
}

bool HistoryStore::restore(qint64 id)
{
    QSqlQuery update(QSqlDatabase::database(m_connection));
    update.prepare(QStringLiteral("UPDATE entries SET deleted_at = NULL WHERE id = ? AND deleted_at IS NOT NULL"));
    update.addBindValue(id);
    if (!m_open || !update.exec() || update.numRowsAffected() == 0) {
        return false;
    }
    forgetRecords();
    emit changed();
    return true;
}

void HistoryStore::purgeDeleted()
{
    if (!m_open) {
        return;
    }
    // An entry whose audio wouldn't go stays hidden, for the next purge.
    const QList<qint64> stayed = deleteAudioWhere(QStringLiteral("deleted_at IS NOT NULL"), {});
    exec(QStringLiteral("DELETE FROM entries WHERE deleted_at IS NOT NULL") + except(stayed));
}

int HistoryStore::countIn(const QDateTime &from, const QDateTime &to) const
{
    Where where;
    where.add(visible);
    where.add(clearedKinds);
    addRange(where, from, to);
    QSqlQuery select(QSqlDatabase::database(m_connection));
    select.prepare(QStringLiteral("SELECT count(*) FROM entries WHERE %1").arg(where.sql()));
    for (const QVariant &value : std::as_const(where.values)) {
        select.addBindValue(value);
    }
    return m_open && select.exec() && select.next() ? select.value(0).toInt() : 0;
}

bool HistoryStore::clear(const QDateTime &from, const QDateTime &to, bool withStats)
{
    if (!m_open) {
        return false;
    }
    Where where;
    where.add(clearedKinds);
    addRange(where, from, to);
    const QList<qint64> stayed = deleteAudioWhere(where.sql(), where.values);
    QSqlQuery change(QSqlDatabase::database(m_connection));
    change.prepare(withStats ? QStringLiteral("DELETE FROM entries WHERE %1%2").arg(where.sql(), except(stayed))
                             : QStringLiteral("UPDATE entries SET text = NULL, raw_text = NULL, app_id = NULL,"
                                              " audio_path = NULL, kept = 'stats' WHERE %1%2")
                                   .arg(where.sql(), except(stayed)));
    for (const QVariant &value : std::as_const(where.values)) {
        change.addBindValue(value);
    }
    if (!change.exec()) {
        qWarning().noquote() << "history could not be cleared:" << change.lastError().text();
        return false;
    }
    // Merging the index drops what it held of the deleted text, and with
    // secure_delete the pages that held it are overwritten.
    if (m_trigram) {
        exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES ('optimize')"));
    }
    if (!from.isValid() && !to.isValid()) {
        exec(QStringLiteral("VACUUM"));
    }
    forgetRecords();
    emit changed();
    return stayed.isEmpty();
}

void HistoryStore::prune(const QDateTime &now, const HistorySettings &settings)
{
    if (!m_open) {
        return;
    }
    int changedRows = 0;
    const auto run = [this, &changedRows](const QString &sql, const QVariantList &values) {
        QSqlQuery change(QSqlDatabase::database(m_connection));
        change.prepare(sql);
        for (const QVariant &value : values) {
            change.addBindValue(value);
        }
        if (change.exec()) {
            changedRows += change.numRowsAffected();
        } else {
            qWarning().noquote() << "history retention failed:" << change.lastError().text();
        }
    };
    const qint64 nowMs = now.toMSecsSinceEpoch();
    if (settings.textDays > 0) {
        const QString expired = clearedKinds
            + QStringLiteral(" AND finished_at < ? AND (text IS NOT NULL OR raw_text IS NOT NULL"
                             " OR app_id IS NOT NULL OR audio_path IS NOT NULL)");
        const QVariantList cutoff{nowMs - settings.textDays * msPerDay};
        const QList<qint64> stayed = deleteAudioWhere(expired, cutoff);
        run(QStringLiteral("UPDATE entries SET text = NULL, raw_text = NULL, app_id = NULL, audio_path = NULL,"
                           " kept = 'stats' WHERE ")
                + expired + except(stayed),
            cutoff);
    }
    const int audioDays = settings.audioDays < 0 ? settings.textDays
        : settings.textDays > 0                  ? qMin(settings.audioDays, settings.textDays)
                                                 : settings.audioDays;
    if (audioDays > 0) {
        const QString expired = clearedKinds + QStringLiteral(" AND audio_path IS NOT NULL AND finished_at < ?");
        const QVariantList cutoff{nowMs - audioDays * msPerDay};
        const QList<qint64> stayed = deleteAudioWhere(expired, cutoff);
        run(QStringLiteral("UPDATE entries SET audio_path = NULL,"
                           " kept = CASE kept WHEN 'audio' THEN 'text' ELSE kept END WHERE ")
                + expired + except(stayed),
            cutoff);
    }
    if (changedRows > 0) {
        if (m_trigram) {
            exec(QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES ('optimize')"));
        }
        emit changed();
    }
}

const QList<DictationRecord> &HistoryStore::records() const
{
    if (m_records) {
        return *m_records;
    }
    m_records.emplace();
    QSqlQuery select(QSqlDatabase::database(m_connection));
    const QString sql = QStringLiteral(
        "SELECT finished_at, utc_offset_s, audio_ms, words, app_name, profile, profile_name, speech, refinement"
        " FROM entries WHERE %1 AND kind IN ('dictation', 'edit') AND outcome IN ('delivered', 'copied')"
        " ORDER BY id");
    if (!m_open || !select.exec(sql.arg(visible))) {
        return *m_records;
    }
    while (select.next()) {
        DictationRecord record;
        record.finishedAt = localTime(select.value(0), select.value(1).toInt());
        record.audioMs = select.value(2).toInt();
        record.words = select.value(3).toInt();
        record.appName = select.value(4).toString();
        record.profile = select.value(5).toString();
        record.profileName = select.value(6).toString();
        record.speechProviders = listFromJson(select.value(7));
        record.refinementProviders = listFromJson(select.value(8));
        m_records->append(record);
    }
    return *m_records;
}

QList<qint64> HistoryStore::deleteAudioWhere(const QString &condition, const QVariantList &values)
{
    QList<qint64> stayed;
    QSqlQuery select(QSqlDatabase::database(m_connection));
    select.prepare(QStringLiteral("SELECT id, audio_path FROM entries WHERE audio_path IS NOT NULL AND ") + condition);
    for (const QVariant &value : values) {
        select.addBindValue(value);
    }
    if (!select.exec()) {
        return stayed;
    }
    while (select.next()) {
        HistoryEntry entry;
        entry.audioPath = select.value(1).toString();
        if (!deleteFile(audioFile(entry))) {
            stayed.append(select.value(0).toLongLong());
        }
    }
    return stayed;
}

QStringList HistoryStore::deleteRecordingFiles(const HistoryEntry &entry) const
{
    QStringList stayed;
    for (const QString &file : {entry.transcriptPath, audioFile(entry)}) {
        if (!deleteFile(file)) {
            stayed.append(file);
        }
    }
    return stayed;
}

bool HistoryStore::deleteFile(const QString &file)
{
    if (file.isEmpty() || !QFile::exists(file) || QFile::remove(file)) {
        return true;
    }
    qWarning().noquote() << "history could not delete file=" + file;
    return false;
}

void HistoryStore::forgetRecords()
{
    m_records.reset();
}

} // namespace speecher
