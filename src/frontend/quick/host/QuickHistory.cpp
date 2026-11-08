#include "frontend/quick/host/QuickHistory.h"

#include "app/ApplicationController.h"
#include "core/InsightsSummary.h"
#include "core/SettingsStore.h"
#include "core/history/HistoryStore.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingSession.h"

#include <QAudioOutput>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QSaveFile>
#include <QUrl>

namespace speecher {
namespace {

// The entry's text as rich text: escaped, so dictated markup shows as
// typed, with each match between \x02 and \x03 set in <b>.
QString matchesHtml(const QString &highlighted)
{
    QString html;
    QString run;
    const auto flush = [&] {
        html += run.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        run.clear();
    };
    for (const QChar c : highlighted) {
        if (c == QChar(0x02) || c == QChar(0x03)) {
            flush();
            html += c == QChar(0x02) ? QStringLiteral("<b>") : QStringLiteral("</b>");
        } else {
            run += c;
        }
    }
    flush();
    return html;
}

// [from, to) of a range by name, as of now; invalid is open.
std::pair<QDateTime, QDateTime> rangeOf(const QString &name, const QDate &today)
{
    const QDateTime startOfToday = today.startOfDay();
    if (name == u"hour") {
        return {QDateTime::currentDateTime().addSecs(-3600), {}};
    }
    if (name == u"today") {
        return {startOfToday, {}};
    }
    if (name == u"yesterday") {
        return {startOfToday.addDays(-1), startOfToday};
    }
    if (name == u"7d") {
        return {startOfToday.addDays(-6), {}};
    }
    if (name == u"30d") {
        return {startOfToday.addDays(-29), {}};
    }
    if (name == u"year") {
        return {QDate(today.year(), 1, 1).startOfDay(), {}};
    }
    return {};
}

} // namespace

QuickHistory::QuickHistory(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_refineAgain(*controller->settings(), *controller->providerRegistry())
{
    connect(&m_refineAgain, &RefineAgain::refined, this, &QuickHistory::refined);
    connect(&m_refineAgain, &RefineAgain::failed, this, &QuickHistory::refineFailed);
    m_searchDelay.setSingleShot(true);
    m_searchDelay.setInterval(searchDelayMs);
    connect(&m_searchDelay, &QTimer::timeout, this, [this] {
        emit searchingChanged();
        search(pageSize);
    });
    m_purge.setSingleShot(true);
    m_purge.setInterval(undoMs);
    connect(&m_purge, &QTimer::timeout, this, [this] { m_controller->history()->purgeDeleted(); });
    // A new entry, a delete or a clear: the same query, as many entries as
    // were showing.
    connect(controller->history(), &HistoryStore::changed, this, [this] {
        search(std::max<int>(pageSize, int(m_results.size())));
        emit changed();
    });
    search(pageSize);
}

QuickHistory::~QuickHistory()
{
    m_controller->history()->purgeDeleted();
}

void QuickHistory::setQuery(const QVariantMap &query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    // Each keystroke restarts the wait, so only the last query runs.
    const bool wasSearching = searching();
    m_searchDelay.start();
    if (!wasSearching) {
        emit searchingChanged();
    }
}

HistoryQuery QuickHistory::currentQuery() const
{
    HistoryQuery query;
    query.text = m_query.value(QStringLiteral("text")).toString();
    const QString kind = m_query.value(QStringLiteral("kind")).toString();
    if (kind == u"dictation") {
        query.kinds = {HistoryKind::Dictation, HistoryKind::Edit};
    } else if (const std::optional<HistoryKind> named = historyKindFromName(kind)) {
        query.kinds = {*named};
    }
    query.app = m_query.value(QStringLiteral("app")).toString();
    query.profile = m_query.value(QStringLiteral("profile")).toString();
    std::tie(query.from, query.to) =
        rangeOf(m_query.value(QStringLiteral("when")).toString(), m_controller->insightsToday());
    return query;
}

void QuickHistory::search(int limit)
{
    HistoryQuery query = currentQuery();
    query.limit = limit;
    const HistoryStore *history = m_controller->history();
    m_results.clear();
    const QStringList terms = historySearchTerms(query.text);
    for (const HistoryHit &hit : history->search(query)) {
        m_results.append(resultOf(hit, terms));
    }
    m_matches = history->count(query);
    emit resultsChanged();
}

void QuickHistory::fetchMore()
{
    if (!hasMore() || searching()) {
        return;
    }
    HistoryQuery query = currentQuery();
    query.limit = pageSize;
    query.beforeId = m_results.last().toMap().value(QStringLiteral("id")).toLongLong();
    const QStringList terms = historySearchTerms(query.text);
    for (const HistoryHit &hit : m_controller->history()->search(query)) {
        m_results.append(resultOf(hit, terms));
    }
    emit resultsChanged();
}

QVariantList QuickHistory::recent(const QString &kind, int limit) const
{
    HistoryQuery query;
    query.limit = limit;
    if (const std::optional<HistoryKind> named = historyKindFromName(kind)) {
        query.kinds = {*named};
    }
    QVariantList results;
    for (const HistoryHit &hit : m_controller->history()->search(query)) {
        results.append(resultOf(hit));
    }
    return results;
}

QVariantMap QuickHistory::resultOf(const HistoryHit &hit, const QStringList &terms) const
{
    const HistoryEntry &entry = hit.entry;
    const bool recording = entry.kind == HistoryKind::Recording;
    QVariantList lines;
    if (recording && !terms.isEmpty()) {
        for (const HistoryLine &line : m_controller->history()->lines(entry.id)) {
            if (lines.size() < 3 && highlightHistoryMatches(line.text, terms) != line.text) {
                lines.append(QVariantMap{{QStringLiteral("atMs"), line.atMs},
                                         {QStringLiteral("speaker"), line.speaker},
                                         {QStringLiteral("text"), line.text}});
            }
        }
    }
    const QDateTime when = entry.stats.finishedAt.isValid() ? entry.stats.finishedAt : entry.startedAt;
    const QList<WritingProfileSettings> profiles = m_controller->settings()->dictationSnapshot().refinement.writingProfiles;
    // A file's audio is the file itself, while it is still there.
    const QString audio = entry.kind == HistoryKind::File ? entry.sourcePath : m_controller->history()->audioFile(entry);
    return {
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("kind"), historyKindName(entry.kind)},
        {QStringLiteral("day"), when.date().toString(Qt::ISODate)},
        {QStringLiteral("startedAt"), entry.startedAt},
        {QStringLiteral("finishedAt"), entry.stats.finishedAt},
        {QStringLiteral("text"), entry.text.value_or(QString())},
        {QStringLiteral("html"), matchesHtml(hit.highlighted)},
        {QStringLiteral("app"), entry.stats.appName},
        {QStringLiteral("appId"), entry.stats.applicationId},
        {QStringLiteral("profile"), entry.stats.profile},
        {QStringLiteral("profileName"), entry.stats.profile.isEmpty() ? QString()
                                                                       : dictationProfileName(entry.stats, profiles)},
        {QStringLiteral("durationMs"), entry.stats.audioMs},
        {QStringLiteral("words"), entry.stats.words},
        {QStringLiteral("outcome"), historyOutcomeName(entry.outcome)},
        {QStringLiteral("hasAudio"), !audio.isEmpty() && QFile::exists(audio)},
        {QStringLiteral("hasRaw"), entry.rawText && !entry.rawText->isEmpty() && entry.rawText != entry.text},
        {QStringLiteral("title"), entry.title},
        {QStringLiteral("transcriptPath"), entry.transcriptPath},
        {QStringLiteral("fileMissing"), recording && !QFile::exists(entry.transcriptPath)},
        {QStringLiteral("live"), recording && !entry.stats.finishedAt.isValid()},
        {QStringLiteral("lines"), lines},
    };
}

int QuickHistory::kept() const
{
    return m_controller->history()->count({});
}

QStringList QuickHistory::apps() const
{
    return m_controller->history()->appNames();
}

QVariantList QuickHistory::profiles() const
{
    const QList<WritingProfileSettings> settings = m_controller->settings()->dictationSnapshot().refinement.writingProfiles;
    QVariantList profiles;
    for (const QString &id : m_controller->history()->profiles()) {
        DictationRecord record;
        record.profile = id;
        profiles.append(QVariantMap{{QStringLiteral("id"), id},
                                    {QStringLiteral("name"), dictationProfileName(record, settings)}});
    }
    return profiles;
}

QVariantMap QuickHistory::playing() const
{
    if (m_playingId == 0) {
        return {};
    }
    return {{QStringLiteral("id"), m_playingId},
            {QStringLiteral("positionMs"), m_player->position()},
            {QStringLiteral("durationMs"), m_player->duration()}};
}

QVariantMap QuickHistory::entry(qint64 id) const
{
    const std::optional<HistoryEntry> entry = m_controller->history()->entry(id);
    if (!entry) {
        return {};
    }
    QVariantMap map = resultOf({*entry, entry->text.value_or(QString())});
    map.insert(QStringLiteral("raw"), entry->rawText.value_or(QString()));
    map.insert(QStringLiteral("speech"), entry->stats.speechProviders);
    map.insert(QStringLiteral("refinement"), entry->stats.refinementProviders);
    map.insert(QStringLiteral("sourcePath"), entry->sourcePath);
    if (entry->kind == HistoryKind::Recording) {
        QVariantList lines;
        for (const HistoryLine &line : m_controller->history()->lines(id)) {
            lines.append(QVariantMap{{QStringLiteral("atMs"), line.atMs},
                                     {QStringLiteral("speaker"), line.speaker},
                                     {QStringLiteral("text"), line.text}});
        }
        map.insert(QStringLiteral("lines"), lines);
        const QString folder = QDir(recordingFolder(m_controller->settings()->dictationSnapshot().recording)).absolutePath();
        map.insert(QStringLiteral("inRecordingsFolder"),
                   QFileInfo(entry->transcriptPath).absolutePath() == folder);
    }
    return map;
}

void QuickHistory::copy(qint64 id, bool raw)
{
    const QVariantMap found = entry(id);
    QGuiApplication::clipboard()->setText(found.value(raw ? QStringLiteral("raw") : QStringLiteral("text")).toString());
}

void QuickHistory::pasteAgain(qint64 id, bool raw)
{
    m_controller->pasteAgain(entry(id).value(raw ? QStringLiteral("raw") : QStringLiteral("text")).toString());
}

int QuickHistory::remove(qint64 id)
{
    if (id == m_playingId) {
        stopPlaying();
    }
    if (!m_controller->history()->markDeleted(id)) {
        return -1;
    }
    m_purge.start();
    return int(id);
}

bool QuickHistory::undo(int token)
{
    return token > 0 && m_controller->history()->restore(token);
}

QStringList QuickHistory::removeRecording(qint64 id, bool withFiles)
{
    HistoryStore *store = m_controller->history();
    const std::optional<HistoryEntry> found = store->entry(id);
    if (!found || found->kind != HistoryKind::Recording) {
        return {};
    }
    if (id == m_playingId) {
        stopPlaying();
    }
    const QStringList stayed = withFiles ? store->deleteRecordingFiles(*found) : QStringList();
    if (stayed.isEmpty()) {
        store->remove(id);
    }
    return stayed;
}

void QuickHistory::openFile(qint64 id) const
{
    if (const std::optional<HistoryEntry> found = m_controller->history()->entry(id)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(found->transcriptPath));
    }
}

void QuickHistory::showInFolder(qint64 id) const
{
    if (const std::optional<HistoryEntry> found = m_controller->history()->entry(id)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(found->transcriptPath).absolutePath()));
    }
}

QUrl QuickHistory::exportFile(qint64 id, const QString &format) const
{
    const std::optional<HistoryEntry> found = m_controller->history()->entry(id);
    if (!found) {
        return {};
    }
    const QFileInfo transcript(found->transcriptPath);
    return QUrl::fromLocalFile(transcript.dir().filePath(transcript.completeBaseName() + QLatin1Char('.') + format));
}

bool QuickHistory::exportEntry(qint64 id, const QString &format, const QUrl &file) const
{
    QByteArray written;
    const QList<HistoryLine> lines = m_controller->history()->lines(id);
    if (format == u"json") {
        QJsonArray array;
        for (const HistoryLine &line : lines) {
            array.append(QJsonObject{{QStringLiteral("atMs"), line.atMs},
                                     {QStringLiteral("speaker"), line.speaker},
                                     {QStringLiteral("text"), line.text}});
        }
        written = QJsonDocument(array).toJson();
    } else {
        for (const HistoryLine &line : lines) {
            written += QStringLiteral("[%1] %2: %3\n").arg(recordingClock(line.atMs), line.speaker, line.text).toUtf8();
        }
    }
    QSaveFile out(file.toLocalFile());
    return out.open(QIODevice::WriteOnly) && out.write(written) == written.size() && out.commit();
}

void QuickHistory::refineAgain(qint64 id, const QString &profile)
{
    const std::optional<HistoryEntry> found = m_controller->history()->entry(id);
    if (!found || !found->rawText) {
        emit refineFailed(id, {});
        return;
    }
    m_refineAgain.refine(id, *found->rawText, profile);
}

bool QuickHistory::keepRefined(qint64 id, const QString &text)
{
    return m_controller->history()->setText(id, text);
}

void QuickHistory::play(qint64 id, qint64 atMs)
{
    const QVariantMap found = entry(id);
    if (!found.value(QStringLiteral("hasAudio")).toBool()) {
        return;
    }
    const std::optional<HistoryEntry> stored = m_controller->history()->entry(id);
    const QString file = stored->kind == HistoryKind::File ? stored->sourcePath
                                                           : m_controller->history()->audioFile(*stored);
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);
        m_player->setAudioOutput(m_output);
        connect(m_player, &QMediaPlayer::positionChanged, this, &QuickHistory::playingChanged);
        connect(m_player, &QMediaPlayer::durationChanged, this, &QuickHistory::playingChanged);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::StoppedState && m_playingId != 0) {
                m_playingId = 0;
                emit playingChanged();
            }
        });
    }
    m_player->stop();
    m_playingId = id;
    m_player->setSource(QUrl::fromLocalFile(file));
    m_player->setPosition(atMs);
    m_player->play();
    emit playingChanged();
}

void QuickHistory::stopPlaying()
{
    if (m_player) {
        m_player->stop();
    }
    if (m_playingId != 0) {
        m_playingId = 0;
        emit playingChanged();
    }
}

int QuickHistory::countIn(const QString &range) const
{
    const auto [from, to] = rangeOf(range, QDate::currentDate());
    return m_controller->history()->countIn(from, to);
}

bool QuickHistory::clear(const QString &range, bool withStats)
{
    const auto [from, to] = rangeOf(range, QDate::currentDate());
    return m_controller->clearHistory(from, to, withStats);
}

} // namespace speecher
