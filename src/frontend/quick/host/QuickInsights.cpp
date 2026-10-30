#include "frontend/quick/host/QuickInsights.h"

#include "app/AccessibilityMonitor.h"
#include "app/ApplicationController.h"
#include "core/InsightsExport.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QmlValues.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <utility>

namespace speecher {

namespace {

// The ids QML uses for the periods and the measures.
constexpr std::array<std::pair<const char *, InsightsRange>, 4> rangeIds{{
    {"7d", InsightsRange::Last7Days},
    {"30d", InsightsRange::Last30Days},
    {"year", InsightsRange::ThisYear},
    {"all", InsightsRange::AllTime},
}};
constexpr std::array<std::pair<const char *, HeatMeasure>, 3> measureIds{{
    {"dictations", HeatMeasure::Dictations},
    {"words", HeatMeasure::Words},
    {"audio", HeatMeasure::Audio},
}};

template <typename Value, std::size_t size>
QString idOf(const std::array<std::pair<const char *, Value>, size> &ids, Value value)
{
    const auto found = std::find_if(ids.begin(), ids.end(), [value](const auto &id) { return id.second == value; });
    return QLatin1String(found->first);
}

template <typename Value, std::size_t size>
std::optional<Value> valueOf(const std::array<std::pair<const char *, Value>, size> &ids, const QString &id)
{
    const auto found = std::find_if(ids.begin(), ids.end(), [&id](const auto &each) { return id == QLatin1String(each.first); });
    return found == ids.end() ? std::nullopt : std::optional<Value>(found->second);
}

QVariant jsDate(const QDate &date)
{
    return date.isValid() ? QVariant(date.startOfDay()) : QVariant();
}

QString milestoneState(Milestone::State state)
{
    switch (state) {
    case Milestone::State::Passed: return QStringLiteral("passed");
    case Milestone::State::Next: return QStringLiteral("next");
    case Milestone::State::Later: break;
    }
    return QStringLiteral("later");
}

QVariantMap summaryMap(const InsightsSummary &summary)
{
    QVariantList apps;
    for (const AppShare &app : summary.apps) {
        apps.append(QVariantMap{{QStringLiteral("name"), app.name},
                                {QStringLiteral("otherApps"), app.otherApps},
                                {QStringLiteral("profile"), app.profileName},
                                {QStringLiteral("words"), app.words},
                                {QStringLiteral("percent"), app.percent}});
    }
    QVariantList profiles;
    for (const ProfileShare &profile : summary.profiles) {
        profiles.append(QVariantMap{{QStringLiteral("name"), profile.name},
                                    {QStringLiteral("words"), profile.words},
                                    {QStringLiteral("percent"), profile.percent}});
    }
    QVariantList hourCounts;
    for (int count : summary.hourCounts) {
        hourCounts.append(count);
    }
    QVariantList milestones;
    for (const Milestone &milestone : summary.milestones) {
        milestones.append(QVariantMap{{QStringLiteral("words"), milestone.words},
                                      {QStringLiteral("state"), milestoneState(milestone.state)},
                                      {QStringLiteral("progress"), milestone.progress}});
    }
    const BookComparison &book = summary.bookComparison;
    return {
        // The period.
        {QStringLiteral("words"), summary.words},
        {QStringLiteral("dictations"), summary.dictations},
        {QStringLiteral("audioMs"), summary.audioMs},
        {QStringLiteral("averageAudioMs"), summary.averageAudioMs},
        {QStringLiteral("dictationsPerActiveDay"), summary.dictationsPerActiveDay},
        // Percent against the previous 7 or 30 days; null for the other
        // periods and when those days were empty.
        {QStringLiteral("wordsDelta"), optionalNumber(summary.wordsDelta)},
        {QStringLiteral("dictationsDelta"), optionalNumber(summary.dictationsDelta)},
        // { book, times, bookWords }: the words are `times` the book's length
        // (see core's BookComparison). Null without a book.
        {QStringLiteral("bookComparison"),
         book.book.isEmpty() ? QVariant()
                             : QVariantMap{{QStringLiteral("book"), book.book},
                                           {QStringLiteral("times"), book.times},
                                           {QStringLiteral("bookWords"), book.bookWords}}},
        // { book, times, at }: the comparison from `at` words on, the next
        // count that compares differently. Null when none does.
        {QStringLiteral("nextBookComparison"),
         summary.bookComparisonChangesAt == 0
             ? QVariant()
             : QVariantMap{{QStringLiteral("book"), summary.nextBookComparison.book},
                           {QStringLiteral("times"), summary.nextBookComparison.times},
                           {QStringLiteral("at"), summary.bookComparisonChangesAt}}},
        {QStringLiteral("wordsPerMinute"), summary.wordsPerMinute},
        {QStringLiteral("typingWordsPerMinute"), summary.typingWordsPerMinute},
        {QStringLiteral("minutesSaved"), summary.minutesSavedVersusTyping},
        // 24 counts by local hour; hasHourData once there are enough to
        // show a pattern. busiestWeekday is 1 (Monday) to 7.
        {QStringLiteral("hourCounts"), hourCounts},
        {QStringLiteral("peakHour"), summary.peakHour},
        {QStringLiteral("busiestWeekday"), int(summary.busiestWeekday)},
        {QStringLiteral("hasHourData"), summary.hasHourData},
        // [{ name, otherApps, profile, words, percent }]: the top five, then
        // the fold of the others, where otherApps counts them. profile is the
        // Writing Profile's name, empty when it was deleted unnamed.
        {QStringLiteral("apps"), apps},
        // [{ name, words, percent }], one per Writing Profile; name as apps' profile.
        {QStringLiteral("profiles"), profiles},
        // All time from here on. A streak lasts until the end of today.
        {QStringLiteral("streak"),
         QVariantMap{{QStringLiteral("current"), summary.currentStreak},
                     {QStringLiteral("best"), summary.bestStreak},
                     {QStringLiteral("bestEnd"), jsDate(summary.bestStreakEnd)},
                     {QStringLiteral("bestEndsToday"), summary.bestStreakEndsToday},
                     {QStringLiteral("dictatedToday"), summary.weekActivity[summary.todayIndex]},
                     // The last run and the first day it was missed, while
                     // current is 0.
                     {QStringLiteral("brokenLength"), summary.brokenStreakLength},
                     {QStringLiteral("brokenEnded"), jsDate(summary.brokenStreakEnded)}}},
        {QStringLiteral("activeDaysLastYear"), summary.activeDaysLastYear},
        {QStringLiteral("allTimeWords"), summary.allTimeWords},
        // [{ words, state: passed|next|later, progress 0..1 }].
        {QStringLiteral("milestones"), milestones},
        {QStringLiteral("longest"),
         QVariantMap{{QStringLiteral("audioMs"), summary.longest.audioMs},
                     {QStringLiteral("words"), summary.longest.words},
                     {QStringLiteral("app"), summary.longest.appName},
                     {QStringLiteral("date"), jsDate(summary.longest.date)}}},
        {QStringLiteral("busiestDay"),
         QVariantMap{{QStringLiteral("date"), jsDate(summary.busiestDay.date)},
                     {QStringLiteral("dictations"), summary.busiestDay.dictations}}},
        {QStringLiteral("wordiestDay"),
         QVariantMap{{QStringLiteral("date"), jsDate(summary.wordiestDay.date)},
                     {QStringLiteral("words"), summary.wordiestDay.words}}},
        {QStringLiteral("firstDictation"), jsDate(summary.firstDictation)},
    };
}

} // namespace

QuickInsights::QuickInsights(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    SettingsStore *settings = controller->settings();
    connect(controller->insightsLog(), &InsightsLog::changed, this, &QuickInsights::refresh);
    // Summarizing every record is too much for every save: only Keep
    // insights, and the profile names the apps and profiles go by, change
    // what shows. The latest dictation's profile goes by its current name.
    connect(settings, &SettingsStore::snapshotApplied, this, [this, settings](const AppSettings &previous) {
        const AppSettings current = settings->dictationSnapshot();
        const bool profilesChanged = current.refinement.writingProfiles != previous.refinement.writingProfiles;
        if (profilesChanged || current.insightsEnabled != previous.insightsEnabled) {
            refresh();
        }
        if (profilesChanged) {
            emit latestChanged();
        }
    });
    connect(settings, &SettingsStore::snapshotApplied, this, &QuickInsights::refreshCorrections);
    connect(settings, &SettingsStore::learnedCorrectionsChanged, this, &QuickInsights::refreshCorrections);
    connect(controller->accessibility(), &AccessibilityMonitor::stateChanged, this, &QuickInsights::refreshCorrections);
    connect(controller, &ApplicationController::lastTranscriptChanged, this, &QuickInsights::latestChanged);
    connect(controller, &ApplicationController::lastRecordChanged, this, &QuickInsights::latestChanged);
    connect(this, &QuickInsights::changed, this, &QuickInsights::heatLevelsChanged);
    connect(this, &QuickInsights::heatMeasureChanged, this, &QuickInsights::heatLevelsChanged);

    m_midnight.setSingleShot(true);
    connect(&m_midnight, &QTimer::timeout, this, &QuickInsights::refresh);
    refresh();
    refreshCorrections();
}

QString QuickInsights::state() const
{
    if (!m_enabled) {
        return QStringLiteral("off");
    }
    return m_controller->insightsLog()->records().isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready");
}

bool QuickInsights::hasData() const
{
    return state() == QLatin1String("ready");
}

int QuickInsights::kept() const
{
    return int(m_controller->insightsLog()->records().size());
}

QVariant QuickInsights::firstDictation() const
{
    const QList<DictationRecord> &records = m_controller->insightsLog()->records();
    const auto first = std::min_element(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return a.finishedAt < b.finishedAt;
    });
    return first == records.end() ? QVariant() : jsDate(first->finishedAt.date());
}

QString QuickInsights::period() const
{
    return idOf(rangeIds, m_range);
}

void QuickInsights::setPeriod(const QString &period)
{
    const std::optional<InsightsRange> range = valueOf(rangeIds, period);
    if (!range) {
        qWarning().noquote() << "Insights: no period" << period;
        return;
    }
    if (*range == m_range) {
        return;
    }
    m_range = *range;
    emit periodChanged();
    refresh();
}

QString QuickInsights::heatMeasure() const
{
    return idOf(measureIds, m_measure);
}

void QuickInsights::setHeatMeasure(const QString &measure)
{
    const std::optional<HeatMeasure> value = valueOf(measureIds, measure);
    if (!value) {
        qWarning().noquote() << "Insights: no heat measure" << measure;
        return;
    }
    if (*value == m_measure) {
        return;
    }
    m_measure = *value;
    emit heatMeasureChanged();
}

QVariant QuickInsights::today() const
{
    return jsDate(m_today);
}

QVariant QuickInsights::summary() const
{
    return m_summaryMap;
}

QVariantList QuickInsights::heatmap() const
{
    return m_heatmap;
}

QVariantList QuickInsights::heatLevels() const
{
    const HeatScale scale(m_summary.heatmap, m_measure);
    QVariantList levels;
    levels.reserve(m_summary.heatmap.size());
    for (const HeatmapDay &day : m_summary.heatmap) {
        levels.append(scale.level(day));
    }
    return levels;
}

QVariantList QuickInsights::monthLabels() const
{
    QVariantList labels;
    const QMap<int, QString> months = speecher::monthLabels(m_summary.heatmap, int((m_summary.heatmap.size() + 6) / 7));
    for (auto it = months.cbegin(); it != months.cend(); ++it) {
        labels.append(QVariantMap{{QStringLiteral("column"), it.key()}, {QStringLiteral("label"), it.value()}});
    }
    return labels;
}

QVariantMap QuickInsights::corrections() const
{
    return m_corrections;
}

QVariant QuickInsights::latest() const
{
    const QString text = m_controller->lastTranscript().trimmed();
    if (text.isEmpty()) {
        return {};
    }
    QVariantMap latest{{QStringLiteral("text"), text},
                       {QStringLiteral("words"), countWords(text)},
                       {QStringLiteral("app"), QString()},
                       {QStringLiteral("profile"), QString()},
                       {QStringLiteral("date"), QVariant()}};
    if (const std::optional<DictationRecord> &record = m_controller->lastRecord()) {
        latest[QStringLiteral("app")] = record->appName;
        latest[QStringLiteral("profile")] = dictationProfileName(*record, m_controller->settings()->dictationSnapshot().refinement.writingProfiles);
        latest[QStringLiteral("date")] = jsDate(record->finishedAt.date());
    }
    return latest;
}

bool QuickInsights::clear()
{
    return m_controller->clearInsights();
}

void QuickInsights::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool QuickInsights::copyImage(const QImage &image)
{
    if (image.isNull()) {
        qWarning().noquote() << "Insights: the share picture came out empty; nothing copied";
        return false;
    }
    QGuiApplication::clipboard()->setImage(image);
    return true;
}

bool QuickInsights::saveJson(const QUrl &file)
{
    const QString path = file.toLocalFile();
    QSaveFile json(path);
    if (!json.open(QIODevice::WriteOnly) || json.write(insightsJson(m_summary, m_range, m_today)) < 0
        || !json.commit()) {
        qWarning().noquote() << "Insights: could not save" << file.toString() << json.errorString();
        return false;
    }
    return true;
}

void QuickInsights::copyLatest()
{
    QGuiApplication::clipboard()->setText(m_controller->lastTranscript().trimmed());
}

void QuickInsights::refresh()
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    m_enabled = settings.insightsEnabled;
    m_today = m_controller->insightsToday();
    const bool ready = hasData();
    m_summary = ready ? summarize(m_controller->insightsLog()->records(), m_range, m_today,
                                  settings.refinement.writingProfiles)
                      : InsightsSummary{};
    m_summaryMap = ready ? QVariant(summaryMap(m_summary)) : QVariant();
    m_heatmap.clear();
    for (const HeatmapDay &day : std::as_const(m_summary.heatmap)) {
        m_heatmap.append(QVariantMap{{QStringLiteral("date"), jsDate(day.date)},
                                     {QStringLiteral("dictations"), day.dictations},
                                     {QStringLiteral("words"), day.words},
                                     {QStringLiteral("audioMs"), day.audioMs}});
    }
    scheduleMidnight();
    emit changed();
}

void QuickInsights::refreshCorrections()
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    QList<LearnedCorrection> learned = settings.learnedCorrections;
    std::stable_sort(learned.begin(), learned.end(), [](const LearnedCorrection &a, const LearnedCorrection &b) {
        return a.createdAtMs > b.createdAtMs;
    });
    QVariantList recent;
    for (const LearnedCorrection &correction : learned.mid(0, 3)) {
        recent.append(QVariantMap{{QStringLiteral("heard"), correction.original},
                                  {QStringLiteral("fixed"), correction.corrected}});
    }
    const QVariantMap corrections = {
        {QStringLiteral("count"), int(learned.size())},
        {QStringLiteral("learning"), settings.correctionLearningEnabled},
        {QStringLiteral("accessible"),
         !m_controller->accessibility()->supported() || m_controller->accessibility()->enabled()},
        {QStringLiteral("recent"), recent},
    };
    // Every settings save comes here.
    if (corrections == m_corrections) {
        return;
    }
    m_corrections = corrections;
    emit correctionsChanged();
}

// Speecher can stay up past midnight; the streak and the periods move with
// the day. A pinned today never moves, but refreshing it costs nothing.
void QuickInsights::scheduleMidnight()
{
    const QDateTime now = QDateTime::currentDateTime();
    m_midnight.start(int(std::max<qint64>(1000, now.msecsTo(now.date().addDays(1).startOfDay()) + 1000)));
}

} // namespace speecher
