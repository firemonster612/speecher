#include "frontend/quick/host/QuickInsights.h"

#include "app/ApplicationController.h"
#include "core/InsightsExport.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <utility>

namespace speecher {

namespace {

QuickInsights *g_instance = nullptr;

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

template <typename Number>
QVariant optionalNumber(const std::optional<Number> &value)
{
    return value ? QVariant::fromValue(*value) : QVariant();
}

// The milestones either side of the all-time words: up to three passed, the
// next with how far along it is, and one after it. Once every milestone is
// passed, the last four.
QVariantList milestoneList(const InsightsSummary &summary)
{
    const auto next = std::find_if(kWordMilestones.begin(), kWordMilestones.end(),
                                   [&](int words) { return words > summary.allTimeWords; });
    const qsizetype nextIndex = next - kWordMilestones.begin();
    const qsizetype first = std::max<qsizetype>(0, nextIndex - 3);
    const qsizetype end = std::min<qsizetype>(qsizetype(kWordMilestones.size()), nextIndex + 2);
    QVariantList list;
    for (qsizetype index = first; index < end; ++index) {
        const int words = kWordMilestones[index];
        const int from = index > 0 ? kWordMilestones[index - 1] : 0;
        const QString state = index < nextIndex ? QStringLiteral("passed")
            : index == nextIndex                ? QStringLiteral("next")
                                                : QStringLiteral("later");
        list.append(QVariantMap{
            {QStringLiteral("words"), words},
            {QStringLiteral("state"), state},
            {QStringLiteral("progress"), std::clamp(double(summary.allTimeWords - from) / (words - from), 0.0, 1.0)},
        });
    }
    return list;
}

QVariantMap summaryMap(const InsightsSummary &summary)
{
    QVariantList apps;
    for (const AppShare &app : summary.apps) {
        apps.append(QVariantMap{{QStringLiteral("name"), app.name},
                                {QStringLiteral("profile"), app.profileLabel},
                                {QStringLiteral("words"), app.words},
                                {QStringLiteral("percent"), app.percent}});
    }
    QVariantList profiles;
    for (const ProfileShare &profile : summary.profiles) {
        profiles.append(QVariantMap{{QStringLiteral("name"), profile.label},
                                    {QStringLiteral("words"), profile.words},
                                    {QStringLiteral("percent"), profile.percent}});
    }
    QVariantList hourCounts;
    for (int count : summary.hourCounts) {
        hourCounts.append(count);
    }
    QVariantList week;
    for (bool active : summary.weekActivity) {
        week.append(active);
    }
    return {
        // The period.
        {QStringLiteral("words"), summary.words},
        {QStringLiteral("dictations"), summary.dictations},
        {QStringLiteral("audioMs"), summary.audioMs},
        {QStringLiteral("activeDays"), summary.activeDays},
        {QStringLiteral("averageAudioMs"), summary.averageAudioMs},
        {QStringLiteral("dictationsPerActiveDay"), summary.dictationsPerActiveDay},
        // Percent against the previous 7 or 30 days; null for the other
        // periods and when those days were empty.
        {QStringLiteral("wordsDelta"), optionalNumber(summary.wordsDelta)},
        {QStringLiteral("dictationsDelta"), optionalNumber(summary.dictationsDelta)},
        // Core's sentence, as the exports carry it: "About half of Hamlet".
        {QStringLiteral("bookComparison"), summary.bookComparison},
        {QStringLiteral("wordsPerMinute"), summary.wordsPerMinute},
        {QStringLiteral("typingWordsPerMinute"), summary.typingWordsPerMinute},
        {QStringLiteral("minutesSaved"), summary.minutesSavedVersusTyping},
        // 24 counts by local hour; hasHourData once there are enough to
        // show a pattern. busiestWeekday is 1 (Monday) to 7.
        {QStringLiteral("hourCounts"), hourCounts},
        {QStringLiteral("peakHour"), summary.peakHour},
        {QStringLiteral("busiestWeekday"), int(summary.busiestWeekday)},
        {QStringLiteral("hasHourData"), summary.hasHourData},
        // [{ name, profile, words, percent }]: the top five, then core's
        // "N other apps" fold, with no profile.
        {QStringLiteral("apps"), apps},
        // [{ name, words, percent }].
        {QStringLiteral("profiles"), profiles},
        // All time from here on. A streak lasts until the end of today.
        {QStringLiteral("streak"),
         QVariantMap{{QStringLiteral("current"), summary.currentStreak},
                     {QStringLiteral("best"), summary.bestStreak},
                     {QStringLiteral("bestEnd"), jsDate(summary.bestStreakEnd)},
                     {QStringLiteral("dictatedToday"), summary.weekActivity[summary.todayIndex]},
                     // The last run and the first day it was missed, while
                     // current is 0.
                     {QStringLiteral("brokenLength"), summary.brokenStreakLength},
                     {QStringLiteral("brokenEnded"), jsDate(summary.brokenStreakEnded)},
                     // Monday to Sunday of this week.
                     {QStringLiteral("week"), week}}},
        {QStringLiteral("activeDaysLastYear"), summary.activeDaysLastYear},
        {QStringLiteral("allTimeWords"), summary.allTimeWords},
        // [{ words, state: passed|next|later, progress 0..1 }].
        {QStringLiteral("milestones"), milestoneList(summary)},
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
    g_instance = this;
    SettingsStore *settings = controller->settings();
    connect(controller->insightsLog(), &InsightsLog::changed, this, &QuickInsights::refresh);
    // Keep insights, and the profile names the apps and profiles go by.
    connect(settings, &SettingsStore::snapshotApplied, this, &QuickInsights::refresh);
    connect(settings, &SettingsStore::snapshotApplied, this, &QuickInsights::refreshCorrections);
    connect(settings, &SettingsStore::learnedCorrectionsChanged, this, &QuickInsights::refreshCorrections);
    connect(settings, &SettingsStore::correctionLearningEnabledChanged, this, &QuickInsights::refreshCorrections);
    connect(controller, &ApplicationController::accessibilityStateChanged, this, &QuickInsights::refreshCorrections);
    connect(controller, &ApplicationController::lastTranscriptChanged, this, &QuickInsights::latestChanged);
    connect(controller, &ApplicationController::lastRecordChanged, this, &QuickInsights::latestChanged);
    connect(this, &QuickInsights::changed, this, &QuickInsights::heatLevelsChanged);
    connect(this, &QuickInsights::heatMeasureChanged, this, &QuickInsights::heatLevelsChanged);

    m_midnight.setSingleShot(true);
    connect(&m_midnight, &QTimer::timeout, this, &QuickInsights::refresh);
    refresh();
    refreshCorrections();
}

QuickInsights::~QuickInsights()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickInsights *QuickInsights::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickInsights::create", "the host makes Insights before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString QuickInsights::state() const
{
    if (!m_controller->settings()->insightsEnabled()) {
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
        latest[QStringLiteral("profile")] = dictationProfileLabel(*record, m_controller->settings()->writingProfileSettings());
        latest[QStringLiteral("date")] = jsDate(record->finishedAt.date());
    }
    return latest;
}

bool QuickInsights::clear()
{
    return m_controller->clearInsights();
}

void QuickInsights::copyText()
{
    QGuiApplication::clipboard()->setText(insightsShareText(m_summary, m_range));
}

void QuickInsights::copyImage(const QImage &image)
{
    if (image.isNull()) {
        qWarning().noquote() << "Insights: the share picture came out empty; nothing copied";
        return;
    }
    QGuiApplication::clipboard()->setImage(image);
}

QString QuickInsights::saveJson()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path =
        QDir(folder).filePath(QStringLiteral("speecher-stats-%1.json").arg(m_today.toString(Qt::ISODate)));
    QDir().mkpath(folder);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(insightsJson(m_summary, m_range, m_today)) < 0
        || !file.commit()) {
        qWarning().noquote() << "Insights: could not save" << path << file.errorString();
        return {};
    }
    return path;
}

void QuickInsights::copyLatest()
{
    QGuiApplication::clipboard()->setText(m_controller->lastTranscript().trimmed());
}

void QuickInsights::refresh()
{
    m_today = m_controller->insightsToday();
    const bool ready = hasData();
    m_summary = ready ? summarize(m_controller->insightsLog()->records(), m_range, m_today,
                                  m_controller->settings()->writingProfileSettings())
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
    SettingsStore *settings = m_controller->settings();
    QList<LearnedCorrection> learned = settings->learnedCorrections();
    std::stable_sort(learned.begin(), learned.end(), [](const LearnedCorrection &a, const LearnedCorrection &b) {
        return a.createdAtMs > b.createdAtMs;
    });
    QVariantList recent;
    for (const LearnedCorrection &correction : learned.mid(0, 3)) {
        recent.append(QVariantMap{{QStringLiteral("heard"), correction.original},
                                  {QStringLiteral("fixed"), correction.corrected}});
    }
    m_corrections = {
        {QStringLiteral("count"), int(learned.size())},
        {QStringLiteral("learning"), settings->correctionLearningEnabled()},
        {QStringLiteral("accessible"),
         !m_controller->accessibilitySupported() || m_controller->accessibilityEnabled()},
        {QStringLiteral("recent"), recent},
    };
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
