#include "frontend/win/HomePage.h"

#include "app/ApplicationController.h"
#include "core/InsightsLog.h"
#include "core/InsightsSummary.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "frontend/win/SettingsPage.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLocale>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Media::Brush;

const QString kGeneralPane = QStringLiteral("general");
const QString kCorrectionsPane = QStringLiteral("corrections");
// InsightsSummary's typing pace, which the Pace card draws a bar for.
constexpr int kTypingWordsPerMinute = 40;
constexpr double kHeatCell = 12;
constexpr double kHourChartHeight = 80;
// The accent over the card at each heat level; level 0 is the empty brush.
constexpr std::array<double, 5> kHeatOpacity{1.0, 0.30, 0.52, 0.76, 1.0};
constexpr double kMutedBarOpacity = 0.42;

const std::array<InsightsRange, 4> kRanges{InsightsRange::Last7Days,
                                           InsightsRange::Last30Days,
                                           InsightsRange::ThisYear,
                                           InsightsRange::AllTime};

QString number(int value)
{
    return QLocale().toString(value);
}

QString plural(int count, const QString &one)
{
    return QStringLiteral("%1 %2").arg(number(count), count == 1 ? one : one + QLatin1Char('s'));
}

// "m:ss".
QString minutesAndSeconds(int ms)
{
    const int seconds = qRound(ms / 1000.0);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString duration(int seconds)
{
    if (seconds < 60) {
        return QStringLiteral("%1s").arg(seconds);
    }
    const int minutes = qRound(seconds / 60.0);
    if (minutes < 60) {
        return QStringLiteral("%1 min").arg(minutes);
    }
    return minutes % 60 ? QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60)
                        : QStringLiteral("%1 h").arg(minutes / 60);
}

QString hourLabel(int hour)
{
    return QStringLiteral("%1 %2").arg(hour % 12 == 0 ? 12 : hour % 12)
        .arg(hour < 12 ? QStringLiteral("am") : QStringLiteral("pm"));
}

QString capitalized(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
}

QString deltaLine(std::optional<int> delta, const QString &period)
{
    if (!delta) {
        return {};
    }
    if (*delta == 0) {
        return QStringLiteral("same as previous %1").arg(period);
    }
    return QStringLiteral("%1 %2% vs previous %3")
        .arg(*delta > 0 ? QStringLiteral("▲") : QStringLiteral("▼"))
        .arg(std::abs(*delta))
        .arg(period);
}

TextBlock secondaryCaption(const QString &text, const PaneHost &host)
{
    return secondaryTextBlock(text, L"SettingsCardDescriptionStyle", host);
}

StackPanel cardBody()
{
    StackPanel body;
    body.Padding({16, 16, 16, 16});
    body.Spacing(8);
    return body;
}

// A card title on the left and a picker on the right.
Grid titledHeader(const UIElement &title, const UIElement &picker)
{
    Grid header;
    ColumnDefinition titleColumn;
    titleColumn.Width({1, GridUnitType::Star});
    ColumnDefinition pickerColumn;
    pickerColumn.Width({0, GridUnitType::Auto});
    header.ColumnDefinitions().Append(titleColumn);
    header.ColumnDefinitions().Append(pickerColumn);
    header.Children().Append(title);
    Grid::SetColumn(picker.as<FrameworkElement>(), 1);
    picker.as<FrameworkElement>().VerticalAlignment(VerticalAlignment::Bottom);
    header.Children().Append(picker);
    return header;
}

// Items as labels; a choice stores the index and rebuilds the page.
ComboBox indexPicker(std::initializer_list<const wchar_t *> labels,
                     int selected,
                     std::function<void(int)> choose)
{
    ComboBox combo;
    for (const wchar_t *label : labels) {
        ComboBoxItem item;
        item.Content(box_value(label));
        combo.Items().Append(item);
    }
    combo.SelectedIndex(selected);
    combo.SelectionChanged([choose = std::move(choose)](const IInspectable &sender, const auto &) {
        const int index = sender.as<ComboBox>().SelectedIndex();
        if (index >= 0) {
            choose(index);
        }
    });
    return combo;
}

// As many equal columns as fit at minWidth each and divide the cards evenly,
// so four tiles go 4, 2 or 1 across and a pair stacks when narrow.
void layoutColumns(const Grid &grid, double width, double minWidth)
{
    const uint32_t count = grid.Children().Size();
    uint32_t columns = std::clamp(static_cast<uint32_t>(width / minWidth), 1u, count);
    while (count % columns) {
        --columns;
    }
    if (grid.ColumnDefinitions().Size() == columns) {
        return;
    }
    grid.ColumnDefinitions().Clear();
    grid.RowDefinitions().Clear();
    for (uint32_t column = 0; column < columns; ++column) {
        ColumnDefinition definition;
        definition.Width({1, GridUnitType::Star});
        grid.ColumnDefinitions().Append(definition);
    }
    for (uint32_t row = 0; row < count / columns; ++row) {
        RowDefinition definition;
        definition.Height({0, GridUnitType::Auto});
        grid.RowDefinitions().Append(definition);
    }
    for (uint32_t index = 0; index < count; ++index) {
        const auto card = grid.Children().GetAt(index).as<FrameworkElement>();
        Grid::SetColumn(card, static_cast<int32_t>(index % columns));
        Grid::SetRow(card, static_cast<int32_t>(index / columns));
    }
}

Grid adaptiveRow(const std::vector<UIElement> &cards, double minWidth)
{
    Grid grid;
    grid.ColumnSpacing(4);
    grid.RowSpacing(4);
    for (const UIElement &card : cards) {
        grid.Children().Append(card);
    }
    layoutColumns(grid, minWidth * cards.size(), minWidth);
    grid.SizeChanged([minWidth](const IInspectable &sender, const SizeChangedEventArgs &args) {
        layoutColumns(sender.as<Grid>(), args.NewSize().Width, minWidth);
    });
    return grid;
}

// Name, bar and trailing value per entry, in one grid so the bars line up.
struct BarEntry {
    UIElement name{nullptr};
    double value = 0;
    double maximum = 0;
    QString trailing;
    QString tip;
};

Grid barTable(const std::vector<BarEntry> &entries, const PaneHost &host)
{
    Grid table;
    table.ColumnSpacing(12);
    table.RowSpacing(8);
    for (const GridUnitType unit : {GridUnitType::Auto, GridUnitType::Star, GridUnitType::Auto}) {
        ColumnDefinition column;
        column.Width({unit == GridUnitType::Star ? 1.0 : 0.0, unit});
        table.ColumnDefinitions().Append(column);
    }
    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const BarEntry &entry = entries.at(row);
        RowDefinition definition;
        definition.Height({0, GridUnitType::Auto});
        table.RowDefinitions().Append(definition);

        const auto name = entry.name.as<FrameworkElement>();
        name.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetRow(name, row);
        table.Children().Append(name);

        ProgressBar bar;
        bar.Minimum(0);
        bar.Maximum(entry.maximum);
        bar.Value(entry.value);
        bar.VerticalAlignment(VerticalAlignment::Center);
        if (!entry.tip.isEmpty()) {
            ToolTipService::SetToolTip(bar, box_value(hs(entry.tip)));
        }
        Grid::SetRow(bar, row);
        Grid::SetColumn(bar, 1);
        table.Children().Append(bar);

        TextBlock trailing = secondaryTextBlock(entry.trailing, L"SettingsInfoTextStyle", host);
        trailing.VerticalAlignment(VerticalAlignment::Center);
        trailing.TextAlignment(TextAlignment::End);
        Grid::SetRow(trailing, row);
        Grid::SetColumn(trailing, 2);
        table.Children().Append(trailing);
    }
    return table;
}

UIElement dictationCard(PaneHost &host, const QDate &today)
{
    ApplicationController *controller = host.controller;
    StackPanel body = cardBody();

    const QString state = controller->stateName().toLower();
    const QString status = state.isEmpty() || state == QStringLiteral("idle")
        ? QStringLiteral("Idle")
        : state == QStringLiteral("listening") ? QStringLiteral("Listening…")
                                               : capitalized(state);
    body.Children().Append(styledTextBlock(status, L"BodyStrongTextBlockStyle"));

    const QString shortcut = controller->globalShortcutDisplay();
    body.Children().Append(secondaryCaption(
        shortcut.isEmpty()
            ? QStringLiteral("Set a Global Shortcut to dictate from anywhere.")
            : QStringLiteral("Press %1 anywhere to dictate into the app you're using.").arg(shortcut),
        host));

    const DictationToggleAction toggleAction = dictationToggleAction(state);
    Button toggle;
    toggle.Content(box_value(hs(toggleAction.label)));
    toggle.IsEnabled(toggleAction.enabled);
    toggle.Click([controller](const auto &, const auto &) { controller->toggle(); });
    body.Children().Append(toggle);

    const QString transcript = controller->session()->lastTranscript();
    if (transcript.isEmpty()) {
        return cardContainer(body);
    }
    // The app and day come from the newest record, which is this transcript's
    // while insights are on.
    QStringList meta{plural(countWords(transcript), QStringLiteral("word"))};
    const QList<DictationRecord> &records = controller->insightsLog()->records();
    if (controller->settings()->insightsEnabled() && !records.isEmpty()) {
        meta << records.last().appName << relativeDay(records.last().finishedAt.date(), today);
    }
    Grid last;
    last.ColumnSpacing(12);
    ColumnDefinition textColumn;
    textColumn.Width({1, GridUnitType::Star});
    ColumnDefinition copyColumn;
    copyColumn.Width({0, GridUnitType::Auto});
    last.ColumnDefinitions().Append(textColumn);
    last.ColumnDefinitions().Append(copyColumn);
    StackPanel text;
    text.Spacing(2);
    TextBlock quote = styledTextBlock(transcript, L"SettingsCardBodyStyle");
    quote.MaxLines(2);
    quote.TextTrimming(TextTrimming::CharacterEllipsis);
    text.Children().Append(quote);
    text.Children().Append(secondaryCaption(meta.join(QStringLiteral(", ")), host));
    last.Children().Append(text);
    Button copy;
    FontIcon copyIcon;
    copyIcon.Glyph(L"\uE8C8");
    copy.Content(copyIcon);
    copy.VerticalAlignment(VerticalAlignment::Top);
    ToolTipService::SetToolTip(copy, box_value(L"Copy transcript"));
    copy.Click([transcript](const auto &, const auto &) {
        QGuiApplication::clipboard()->setText(transcript);
    });
    Grid::SetColumn(copy, 1);
    last.Children().Append(copy);
    body.Children().Append(last);
    return cardContainer(body);
}

// "Insights are off" or "No insights yet": a title over a sentence.
StackPanel notice(const QString &title, const QString &text, const PaneHost &host)
{
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(title, L"BodyStrongTextBlockStyle"));
    body.Children().Append(secondaryCaption(text, host));
    return body;
}

// A tile's label, value and secondary lines; a line's tip becomes its tooltip.
StackPanel statTile(const QString &label,
                    const QString &value,
                    const QList<QPair<QString, QString>> &lines,
                    const PaneHost &host)
{
    StackPanel tile = cardBody();
    tile.Spacing(4);
    tile.Children().Append(secondaryCaption(label, host));
    tile.Children().Append(styledTextBlock(value, L"SubtitleTextBlockStyle"));
    for (const auto &[text, tip] : lines) {
        if (text.isEmpty()) {
            continue;
        }
        TextBlock line = secondaryCaption(text, host);
        if (!tip.isEmpty()) {
            ToolTipService::SetToolTip(line, box_value(hs(tip)));
        }
        tile.Children().Append(line);
    }
    return tile;
}

QString streakLine(const InsightsSummary &summary, const QDate &today)
{
    if (summary.currentStreak > 0) {
        if (!summary.weekActivity.at(summary.todayIndex)) {
            return QStringLiteral("Dictate today to keep it going");
        }
        return summary.currentStreak >= summary.bestStreak
            ? QStringLiteral("Your longest yet")
            : QStringLiteral("Best: %1").arg(plural(summary.bestStreak, QStringLiteral("day")));
    }
    if (summary.brokenStreakLength > 0) {
        return QStringLiteral("%1-day run ended %2")
            .arg(summary.brokenStreakLength)
            .arg(relativeDay(summary.brokenStreakEnded, today));
    }
    return {};
}

// This week, Monday first: a dot per day so far, filled on days with
// dictation, today ringed (in the text colour when its dot is already accent).
StackPanel weekDots(const InsightsSummary &summary, const Brush &accent, const Brush &empty,
                    const PaneHost &host)
{
    StackPanel dots;
    dots.Orientation(Orientation::Horizontal);
    dots.Spacing(6);
    const QLocale locale;
    for (int day = 0; day < 7; ++day) {
        StackPanel column;
        column.Spacing(2);
        Border dot;
        dot.Width(kHeatCell);
        dot.Height(kHeatCell);
        dot.CornerRadius({kHeatCell / 2, kHeatCell / 2, kHeatCell / 2, kHeatCell / 2});
        const bool active = summary.weekActivity.at(day);
        if (day <= summary.todayIndex) {
            dot.Background(active ? accent : empty);
        }
        if (day == summary.todayIndex) {
            dot.BorderBrush(active ? themeBrush(L"SettingsCardDescriptionForeground", host) : accent);
            dot.BorderThickness({2, 2, 2, 2});
        }
        column.Children().Append(dot);
        TextBlock letter = secondaryCaption(locale.dayName(day + 1, QLocale::NarrowFormat), host);
        letter.HorizontalAlignment(HorizontalAlignment::Center);
        column.Children().Append(letter);
        dots.Children().Append(column);
    }
    return dots;
}

UIElement statTiles(const InsightsSummary &summary, const QDate &today, const PaneHost &host,
                    const Brush &accent, const Brush &empty)
{
    StackPanel words = statTile(
        QStringLiteral("Words dictated"),
        number(summary.words),
        {{summary.bookComparison, summary.bookComparisonTip},
         {deltaLine(summary.wordsDelta, summary.deltaPeriodLabel), {}}},
        host);

    StackPanel streak = statTile(QStringLiteral("Streak"),
                                 plural(summary.currentStreak, QStringLiteral("day")),
                                 {{streakLine(summary, today), {}}},
                                 host);
    streak.Children().Append(weekDots(summary, accent, empty, host));

    StackPanel dictations = statTile(
        QStringLiteral("Dictations"),
        number(summary.dictations),
        {{summary.activeDays > 0
              ? QStringLiteral("%1 a day when you dictate")
                    .arg(QLocale().toString(summary.dictationsPerActiveDay, 'f', 1))
              : QStringLiteral("Nothing yet"),
          {}},
         {deltaLine(summary.dictationsDelta, summary.deltaPeriodLabel), {}}},
        host);

    constexpr int hourMs = 3600 * 1000;
    StackPanel audio = statTile(
        QStringLiteral("Audio transcribed"),
        summary.audioMs >= hourMs
            ? QStringLiteral("%1 hours").arg(QLocale().toString(double(summary.audioMs) / hourMs, 'f', 1))
            : QStringLiteral("%1 min").arg(qRound(summary.audioMs / 60000.0)),
        {{summary.dictations > 0
              ? QStringLiteral("Average dictation %1").arg(minutesAndSeconds(summary.averageAudioMs))
              : QStringLiteral("Nothing yet"),
          {}}},
        host);

    return adaptiveRow({cardContainer(words), cardContainer(streak), cardContainer(dictations),
                        cardContainer(audio)},
                       170);
}

Border heatCell(int level, const Brush &accent, const Brush &empty)
{
    Border cell;
    cell.Width(kHeatCell);
    cell.Height(kHeatCell);
    cell.CornerRadius({2, 2, 2, 2});
    cell.Background(level == 0 ? empty : accent);
    cell.Opacity(kHeatOpacity.at(level));
    return cell;
}

int measureValue(const HeatmapDay &day, int measure)
{
    switch (measure) {
    case 1:
        return day.words;
    case 2:
        return day.audioMs;
    default:
        return day.dictations;
    }
}

QString describeDay(const HeatmapDay &day, int measure)
{
    if (day.dictations == 0) {
        return QStringLiteral("No dictation");
    }
    const QString dictations = plural(day.dictations, QStringLiteral("dictation"));
    const QString words = plural(day.words, QStringLiteral("word"));
    switch (measure) {
    case 1:
        return QStringLiteral("%1 from %2").arg(words, dictations);
    case 2:
        return QStringLiteral("%1 of audio").arg(duration(qRound(day.audioMs / 1000.0)));
    default:
        return QStringLiteral("%1, %2").arg(dictations, words);
    }
}

// GitHub's layout: a column per Monday-first week, Mon/Wed/Fri on the left,
// month names over the week each month starts in.
UIElement heatmapGrid(const InsightsSummary &summary, int measure, const PaneHost &host,
                      const Brush &accent, const Brush &empty)
{
    const QList<HeatmapDay> &days = summary.heatmap;
    const int weeks = static_cast<int>((days.size() + 6) / 7);
    Grid grid;
    grid.ColumnSpacing(3);
    grid.RowSpacing(3);
    ColumnDefinition labelColumn;
    labelColumn.Width({0, GridUnitType::Auto});
    grid.ColumnDefinitions().Append(labelColumn);
    for (int week = 0; week < weeks; ++week) {
        ColumnDefinition column;
        column.Width({kHeatCell, GridUnitType::Pixel});
        grid.ColumnDefinitions().Append(column);
    }
    RowDefinition monthRow;
    monthRow.Height({0, GridUnitType::Auto});
    grid.RowDefinitions().Append(monthRow);
    for (int row = 0; row < 7; ++row) {
        RowDefinition definition;
        definition.Height({kHeatCell, GridUnitType::Pixel});
        grid.RowDefinitions().Append(definition);
    }

    const QLocale locale;
    // Row 1 is Monday. Two rows tall, so the caption is not clipped to a cell.
    for (const int weekday : {Qt::Monday, Qt::Wednesday, Qt::Friday}) {
        TextBlock text = secondaryCaption(locale.dayName(weekday, QLocale::ShortFormat), host);
        text.VerticalAlignment(VerticalAlignment::Top);
        Grid::SetRow(text, weekday);
        Grid::SetRowSpan(text, 2);
        grid.Children().Append(text);
    }

    QList<int> activeValues;
    for (const HeatmapDay &day : days) {
        if (day.dictations > 0) {
            activeValues.append(measureValue(day, measure));
        }
    }
    int lastMonth = -1;
    for (int week = 0; week < weeks; ++week) {
        const QDate monday = days.at(week * 7).date;
        if (monday.month() != lastMonth && week < weeks - 2) {
            if (lastMonth != -1 || monday.day() <= 7) {
                TextBlock month = secondaryCaption(
                    locale.standaloneMonthName(monday.month(), QLocale::ShortFormat), host);
                month.TextWrapping(TextWrapping::NoWrap);
                Grid::SetColumn(month, week + 1);
                Grid::SetColumnSpan(month, std::min(4, weeks - week));
                grid.Children().Append(month);
            }
            lastMonth = monday.month();
        }
    }
    for (int index = 0; index < days.size(); ++index) {
        const HeatmapDay &day = days.at(index);
        Border cell = heatCell(heatLevel(measureValue(day, measure), activeValues), accent, empty);
        ToolTipService::SetToolTip(
            cell,
            box_value(hs(describeDay(day, measure) + QLatin1Char('\n')
                         + locale.toString(day.date, QStringLiteral("ddd, MMM d, yyyy")))));
        Grid::SetColumn(cell, index / 7 + 1);
        Grid::SetRow(cell, index % 7 + 1);
        grid.Children().Append(cell);
    }
    // A narrow window shrinks the year rather than cutting weeks off.
    Viewbox fit;
    fit.StretchDirection(StretchDirection::DownOnly);
    fit.HorizontalAlignment(HorizontalAlignment::Left);
    fit.Child(grid);
    return fit;
}

UIElement activityCard(const InsightsSummary &summary, PaneHost &host,
                       const Brush &accent, const Brush &empty)
{
    StackPanel body = cardBody();
    body.Spacing(12);
    body.Children().Append(titledHeader(
        styledTextBlock(QStringLiteral("Activity"), L"BodyStrongTextBlockStyle"),
        indexPicker({L"Dictations", L"Words", L"Minutes of audio"},
                    host.homeMeasure,
                    [&host](int index) {
                        host.homeMeasure = index;
                        host.refresh();
                    })));
    body.Children().Append(heatmapGrid(summary, host.homeMeasure, host, accent, empty));

    StackPanel legend;
    legend.Orientation(Orientation::Horizontal);
    legend.Spacing(3);
    legend.Children().Append(secondaryCaption(QStringLiteral("Less"), host));
    for (int level = 0; level < static_cast<int>(kHeatOpacity.size()); ++level) {
        Border cell = heatCell(level, accent, empty);
        cell.VerticalAlignment(VerticalAlignment::Center);
        legend.Children().Append(cell);
    }
    legend.Children().Append(secondaryCaption(QStringLiteral("More"), host));
    body.Children().Append(titledHeader(
        secondaryCaption(QStringLiteral("%1 with dictation in the last year")
                             .arg(plural(summary.activeDaysLastYear, QStringLiteral("day"))),
                         host),
        legend));
    return cardContainer(body);
}

UIElement hourChart(const InsightsSummary &summary, const PaneHost &host, const Brush &accent)
{
    Grid chart;
    chart.ColumnSpacing(2);
    RowDefinition barRow;
    barRow.Height({kHourChartHeight, GridUnitType::Pixel});
    RowDefinition labelRow;
    labelRow.Height({0, GridUnitType::Auto});
    chart.RowDefinitions().Append(barRow);
    chart.RowDefinitions().Append(labelRow);
    const int most = *std::max_element(summary.hourCounts.begin(), summary.hourCounts.end());
    for (int hour = 0; hour < 24; ++hour) {
        ColumnDefinition column;
        column.Width({1, GridUnitType::Star});
        chart.ColumnDefinitions().Append(column);
        const int count = summary.hourCounts.at(hour);
        Border bar;
        bar.VerticalAlignment(VerticalAlignment::Bottom);
        bar.Height(count > 0 && most > 0 ? std::max(3.0, kHourChartHeight * count / most) : 0);
        bar.CornerRadius({2, 2, 0, 0});
        bar.Background(accent);
        bar.Opacity(hour == summary.peakHour ? 1.0 : kMutedBarOpacity);
        ToolTipService::SetToolTip(
            bar,
            box_value(hs(QStringLiteral("%1 to %2\n%3")
                             .arg(hourLabel(hour), hourLabel((hour + 1) % 24),
                                  plural(count, QStringLiteral("dictation"))))));
        Grid::SetColumn(bar, hour);
        chart.Children().Append(bar);
    }
    for (const int hour : {0, 6, 12, 18}) {
        TextBlock tick = secondaryCaption(hourLabel(hour), host);
        tick.TextWrapping(TextWrapping::NoWrap);
        Grid::SetRow(tick, 1);
        Grid::SetColumn(tick, hour);
        Grid::SetColumnSpan(tick, 6);
        chart.Children().Append(tick);
    }
    return chart;
}

UIElement hoursCard(const InsightsSummary &summary, const PaneHost &host, const Brush &accent)
{
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(QStringLiteral("When you talk"), L"BodyStrongTextBlockStyle"));
    if (!summary.hasHourData) {
        body.Children().Append(secondaryCaption(
            QStringLiteral("After a few days of dictation this shows the hours you talk most."), host));
        return cardContainer(body);
    }
    body.Children().Append(styledTextBlock(summary.persona + QLatin1Char('.'), L"SettingsCardBodyStyle"));
    body.Children().Append(secondaryCaption(
        QStringLiteral("You dictate most around %1, and %2s are your busiest day.")
            .arg(hourLabel(summary.peakHour),
                 QLocale().dayName(summary.busiestWeekday, QLocale::LongFormat)),
        host));
    body.Children().Append(hourChart(summary, host, accent));
    return cardContainer(body);
}

UIElement emptyPeriodCard(const QString &title, const PaneHost &host)
{
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(title, L"BodyStrongTextBlockStyle"));
    body.Children().Append(secondaryCaption(QStringLiteral("No dictation in this period."), host));
    return cardContainer(body);
}

// A large number over its caption, as the Pace and Corrections cards show them.
StackPanel figure(const QString &value, const QString &caption, const PaneHost &host)
{
    StackPanel stack;
    stack.Children().Append(styledTextBlock(value, L"SubtitleTextBlockStyle"));
    stack.Children().Append(secondaryCaption(caption, host));
    return stack;
}

UIElement paceCard(const InsightsSummary &summary, const PaneHost &host)
{
    if (summary.dictations == 0) {
        return emptyPeriodCard(QStringLiteral("Pace"), host);
    }
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(QStringLiteral("Pace"), L"BodyStrongTextBlockStyle"));
    StackPanel figures;
    figures.Orientation(Orientation::Horizontal);
    figures.Spacing(32);
    figures.Children().Append(figure(QStringLiteral("%1 wpm").arg(summary.wordsPerMinute),
                                     QStringLiteral("Your speaking pace"), host));
    figures.Children().Append(figure(duration(summary.minutesSavedVersusTyping * 60),
                                     QStringLiteral("Saved over typing"), host));
    body.Children().Append(figures);
    const double scale = std::max(summary.wordsPerMinute, 160);
    body.Children().Append(barTable(
        {{styledTextBlock(QStringLiteral("You, speaking"), L"SettingsCardBodyStyle"),
          double(summary.wordsPerMinute), scale, number(summary.wordsPerMinute), {}},
         {styledTextBlock(QStringLiteral("Typical typing"), L"SettingsCardBodyStyle"),
          double(kTypingWordsPerMinute), scale, number(kTypingWordsPerMinute), {}}},
        host));
    body.Children().Append(secondaryCaption(
        QStringLiteral("That's %1× faster than typing at %2 words per minute.")
            .arg(QLocale().toString(double(summary.wordsPerMinute) / kTypingWordsPerMinute, 'f', 1))
            .arg(kTypingWordsPerMinute),
        host));
    return cardContainer(body);
}

UIElement appsCard(const InsightsSummary &summary, const PaneHost &host)
{
    if (summary.apps.isEmpty()) {
        return emptyPeriodCard(QStringLiteral("Where your words go"), host);
    }
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(QStringLiteral("Where your words go"), L"BodyStrongTextBlockStyle"));
    std::vector<BarEntry> entries;
    for (const AppShare &app : summary.apps) {
        StackPanel name;
        name.Children().Append(styledTextBlock(app.name, L"SettingsCardBodyStyle"));
        if (!app.profileLabel.isEmpty()) {
            name.Children().Append(secondaryCaption(app.profileLabel, host));
        }
        entries.push_back({name, double(app.words), double(summary.apps.first().words),
                           QStringLiteral("%1%").arg(app.percent),
                           plural(app.words, QStringLiteral("word"))});
    }
    body.Children().Append(barTable(entries, host));
    return cardContainer(body);
}

UIElement correctionsCard(PaneHost &host)
{
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(QStringLiteral("Corrections"), L"BodyStrongTextBlockStyle"));
    const int learned = static_cast<int>(host.controller->settings()->learnedCorrections().size());
    body.Children().Append(figure(number(learned), QStringLiteral("Corrections learned"), host));
    body.Children().Append(secondaryCaption(
        QStringLiteral("Speecher learned these from edits you made after dictating."), host));
    HyperlinkButton open;
    open.Content(box_value(L"Review learned corrections"));
    open.Padding({0, 2, 0, 0});
    open.Click([&host](const auto &, const auto &) { host.showPane(kCorrectionsPane); });
    body.Children().Append(open);
    return cardContainer(body);
}

UIElement recordsCard(const InsightsSummary &summary, const QDate &today, PaneHost &host)
{
    StackPanel rows;
    const auto append = [&rows, &host](const QString &label, const QString &help, const UIElement &control) {
        RowSnapshot row;
        row.label = label;
        row.help = help;
        rows.Children().Append(rowGrid(row, control, host, rows.Children().Size() > 0));
    };
    const auto value = [&host](const QString &text) {
        return secondaryTextBlock(text, L"SettingsInfoTextStyle", host);
    };
    if (summary.nextMilestone > 0) {
        ProgressBar progress;
        progress.Width(160);
        progress.Minimum(0);
        progress.Maximum(summary.nextMilestone);
        progress.Value(summary.allTimeWords);
        QString help = QStringLiteral("%1 to go").arg(number(summary.nextMilestone - summary.allTimeWords));
        if (summary.passedMilestone) {
            help += QStringLiteral(". You passed %1 already.").arg(number(*summary.passedMilestone));
        }
        append(QStringLiteral("Next milestone: %1 words").arg(number(summary.nextMilestone)), help, progress);
    }
    append(QStringLiteral("Longest streak"),
           summary.bestStreakEndsToday
               ? QStringLiteral("That's the one you're on")
               : QStringLiteral("Ended %1").arg(relativeDay(summary.bestStreakEnd, today)),
           value(plural(summary.bestStreak, QStringLiteral("day"))));
    append(QStringLiteral("Longest dictation"),
           QStringLiteral("%1 words into %2, %3")
               .arg(number(summary.longest.words), summary.longest.appName,
                    relativeDay(summary.longest.date, today)),
           value(minutesAndSeconds(summary.longest.audioMs)));
    append(QStringLiteral("Busiest day"),
           capitalized(relativeDay(summary.busiestDay.date, today)),
           value(plural(summary.busiestDay.dictations, QStringLiteral("dictation"))));
    append(QStringLiteral("Wordiest day"),
           capitalized(relativeDay(summary.wordiestDay.date, today)),
           value(plural(summary.wordiestDay.words, QStringLiteral("word"))));
    const int daysSinceFirst = static_cast<int>(summary.firstDictation.daysTo(today));
    append(QStringLiteral("First dictation"),
           QLocale().toString(summary.firstDictation, QStringLiteral("MMM d, yyyy")),
           value(daysSinceFirst == 0
                     ? QStringLiteral("Today")
                     : QStringLiteral("%1 ago").arg(plural(daysSinceFirst, QStringLiteral("day")))));
    return cardContainer(rows);
}

// The lock, the sentence (wrapping when narrow) and the link under it.
UIElement privacyFooter(PaneHost &host)
{
    Grid footer;
    footer.Margin({1, 12, 0, 0});
    footer.ColumnSpacing(6);
    ColumnDefinition lockColumn;
    lockColumn.Width({0, GridUnitType::Auto});
    ColumnDefinition textColumn;
    textColumn.Width({1, GridUnitType::Star});
    footer.ColumnDefinitions().Append(lockColumn);
    footer.ColumnDefinitions().Append(textColumn);
    for (int row = 0; row < 2; ++row) {
        RowDefinition definition;
        definition.Height({0, GridUnitType::Auto});
        footer.RowDefinitions().Append(definition);
    }
    FontIcon lock;
    lock.Glyph(L"\uE72E");
    lock.FontSize(12);
    if (const auto brush = themeBrush(L"SettingsCardDescriptionForeground", host)) {
        lock.Foreground(brush);
    }
    lock.VerticalAlignment(VerticalAlignment::Center);
    footer.Children().Append(lock);
    TextBlock text = secondaryCaption(
        QStringLiteral("Insights are stored only on this computer and are never sent to the cloud."), host);
    Grid::SetColumn(text, 1);
    footer.Children().Append(text);
    HyperlinkButton settings;
    settings.Content(box_value(L"Insights settings"));
    settings.Padding({0, 2, 0, 0});
    settings.Click([&host](const auto &, const auto &) { host.showPane(kGeneralPane); });
    Grid::SetRow(settings, 1);
    Grid::SetColumn(settings, 1);
    footer.Children().Append(settings);
    return footer;
}

} // namespace

UIElement buildHomePage(PaneHost &host)
{
    ApplicationController *controller = host.controller;
    const QDate today = controller->insightsToday();
    StackPanel column;
    ScrollViewer scroll = pageScaffold(QStringLiteral("Home"), column);
    StackPanel cards;
    cards.Spacing(4);
    cards.Margin({0, 12, 0, 0});
    cards.Children().Append(dictationCard(host, today));
    column.Children().Append(cards);

    const QList<DictationRecord> &records = controller->insightsLog()->records();
    if (!controller->settings()->insightsEnabled()) {
        StackPanel off = notice(
            QStringLiteral("Insights are off"),
            QStringLiteral("Speecher isn't keeping any record of your dictation. If you turn "
                           "insights on, your stats are stored only on this computer and never "
                           "sent to the cloud."),
            host);
        Button settings;
        settings.Content(box_value(L"Insights settings…"));
        settings.Click([&host](const auto &, const auto &) { host.showPane(kGeneralPane); });
        off.Children().Append(settings);
        cards.Children().Append(cardContainer(off));
        return scroll;
    }
    if (records.isEmpty()) {
        cards.Children().Append(cardContainer(notice(
            QStringLiteral("No insights yet"),
            QStringLiteral("Your stats appear here after your next dictation. They're stored "
                           "only on this computer and never sent to the cloud."),
            host)));
        return scroll;
    }

    const InsightsSummary summary = summarize(records, host.homeRange, today);
    const Brush accent = themeBrush(L"HomeChartAccentBrush", host);
    const Brush empty = themeBrush(L"HomeChartEmptyBrush", host);

    const int rangeIndex = static_cast<int>(
        std::find(kRanges.begin(), kRanges.end(), host.homeRange) - kRanges.begin());
    column.Children().Append(titledHeader(
        styledTextBlock(QStringLiteral("Your dictation"), L"SettingsSectionHeaderStyle"),
        indexPicker({L"Last 7 days", L"Last 30 days", L"This year", L"All time"},
                    rangeIndex,
                    [&host](int index) {
                        host.homeRange = kRanges.at(index);
                        host.refresh();
                    })));

    StackPanel insights;
    insights.Spacing(4);
    insights.Children().Append(statTiles(summary, today, host, accent, empty));
    insights.Children().Append(activityCard(summary, host, accent, empty));
    insights.Children().Append(adaptiveRow({hoursCard(summary, host, accent), paceCard(summary, host)}, 320));
    insights.Children().Append(adaptiveRow({appsCard(summary, host), correctionsCard(host)}, 320));
    column.Children().Append(insights);

    column.Children().Append(styledTextBlock(QStringLiteral("Records"), L"SettingsSectionHeaderStyle"));
    column.Children().Append(recordsCard(summary, today, host));
    column.Children().Append(privacyFooter(host));
    return scroll;
}

} // namespace speecher::win
