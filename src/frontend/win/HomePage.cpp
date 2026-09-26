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
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Media::Brush;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;

const QString kGeneralPane = QStringLiteral("general");
const QString kCorrectionsPane = QStringLiteral("corrections");
constexpr double kHeatCell = 12;
constexpr double kHeatGap = 3;
// Wide enough for "Wed" in the caption style.
constexpr double kHeatLabelWidth = 32;
constexpr double kHourChartHeight = 80;
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

QString duration(qint64 seconds)
{
    if (seconds < 60) {
        return QStringLiteral("%1s").arg(seconds);
    }
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60) {
        return QStringLiteral("%1 min").arg(minutes);
    }
    return minutes % 60 ? QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60)
                        : QStringLiteral("%1 h").arg(minutes / 60);
}

// Home's chart colours, built from the system accent in code. As brushes in
// styles.xaml they took their colour from a ThemeResource inside a theme
// dictionary fetched in code, which drew nothing. The accent is Dark1 on Light
// and Light2 on Dark, as AccentFillColorDefaultBrush picks; the empty cell a
// faint text-on-card tint; a contrast theme uses Highlight and GrayText.
struct ChartBrushes {
    Brush accent{nullptr};
    Brush empty{nullptr};
};

ChartBrushes chartBrushes(const PaneHost &host)
{
    using namespace winrt::Windows::UI::ViewManagement;
    const UISettings system;
    if (highContrastOn()) {
        return {SolidColorBrush(system.UIElementColor(UIElementType::Highlight)),
                SolidColorBrush(system.UIElementColor(UIElementType::GrayText))};
    }
    // Default counts as Dark, as themeBrush reads it.
    const bool light = host.effectiveTheme && host.effectiveTheme() == ElementTheme::Light;
    return {SolidColorBrush(system.GetColorValue(light ? UIColorType::AccentDark1
                                                       : UIColorType::AccentLight2)),
            SolidColorBrush(light ? winrt::Windows::UI::Color{0x18, 0x00, 0x00, 0x00}
                                  : winrt::Windows::UI::Color{0x15, 0xFF, 0xFF, 0xFF})};
}

QString capitalized(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
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
    QStringList meta{plural(countWords(transcript), QStringLiteral("word"))};
    if (const std::optional<DictationRecord> &record = controller->lastRecord()) {
        meta << record->appName << relativeDay(record->finishedAt.date(), today);
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
            // Today is ringed in the accent with a card-coloured gap, so the
            // ring reads on a filled dot and an empty one alike. Whole pixels
            // only: a half-pixel ring or gap rounds differently on each side
            // and pushes the dot off centre.
            constexpr double ring = kHeatCell + 6;
            Border halo;
            halo.Width(ring);
            halo.Height(ring);
            halo.CornerRadius({ring / 2, ring / 2, ring / 2, ring / 2});
            halo.BorderBrush(accent);
            halo.BorderThickness({1, 1, 1, 1});
            halo.Padding({2, 2, 2, 2});
            halo.Child(dot);
            halo.HorizontalAlignment(HorizontalAlignment::Center);
            column.Children().Append(halo);
        } else {
            dot.Margin({3, 3, 3, 3});
            column.Children().Append(dot);
        }
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
         {deltaText(summary.wordsDelta, summary.deltaPeriodLabel), {}}},
        host);

    StackPanel streak = statTile(QStringLiteral("Streak"),
                                 plural(summary.currentStreak, QStringLiteral("day")),
                                 {{streakText(summary, today), {}}},
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
         {deltaText(summary.dictationsDelta, summary.deltaPeriodLabel), {}}},
        host);

    StackPanel audio = statTile(QStringLiteral("Audio transcribed"),
                                audioTotalText(summary.audioMs),
                                {{averageDictationText(summary), {}}},
                                host);

    return adaptiveRow({cardContainer(words), cardContainer(streak), cardContainer(dictations),
                        cardContainer(audio)},
                       170);
}

// A chart mark's tip, shown the moment the pointer arrives rather than after
// WinUI's hover delay, as on the other platforms.
void setImmediateTip(const FrameworkElement &mark, const QString &text)
{
    ToolTip tip;
    tip.Content(box_value(hs(text)));
    ToolTipService::SetToolTip(mark, tip);
    mark.PointerEntered([tip](const IInspectable &, const auto &) { tip.IsOpen(true); });
    mark.PointerExited([tip](const IInspectable &, const auto &) { tip.IsOpen(false); });
}

// A heatmap cell that outlines itself in the text colour while the pointer is
// on it, which reads on every level from empty to full accent. The tint and
// the outline are separate layers: the tint's opacity is the level, and the
// outline must not fade with it.
Grid hoverableCell(const Border &tint)
{
    Border outline;
    outline.CornerRadius({2, 2, 2, 2});
    outline.BorderThickness({1.5, 1.5, 1.5, 1.5});
    Grid cell;
    // Half the gap on each side belongs to this day, so a pointer between two
    // cells still hovers one: the tip stays and the outline does not drop.
    // The negative margin gives the room back, so the grid does not grow.
    cell.Width(kHeatCell + kHeatGap);
    cell.Height(kHeatCell + kHeatGap);
    cell.Margin({-kHeatGap / 2, -kHeatGap / 2, -kHeatGap / 2, -kHeatGap / 2});
    cell.Padding({kHeatGap / 2, kHeatGap / 2, kHeatGap / 2, kHeatGap / 2});
    // Transparent, not empty: an unfilled Grid lets the pointer through.
    cell.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
    cell.Children().Append(tint);
    cell.Children().Append(outline);
    const auto resources = Application::Current().Resources();
    const auto key = box_value(hstring(L"TextFillColorPrimaryBrush"));
    if (resources.HasKey(key)) {
        const Brush brush = resources.Lookup(key).as<Brush>();
        cell.PointerEntered([outline, brush](const IInspectable &, const auto &) {
            outline.BorderBrush(brush);
        });
        cell.PointerExited([outline](const IInspectable &, const auto &) {
            outline.BorderBrush(nullptr);
        });
    }
    return cell;
}

Border heatCell(int level, const Brush &accent, const Brush &empty)
{
    Border cell;
    cell.Width(kHeatCell);
    cell.Height(kHeatCell);
    cell.CornerRadius({2, 2, 2, 2});
    cell.Background(level == 0 ? empty : accent);
    cell.Opacity(level == 0 ? 1.0 : kHeatStrengths.at(level));
    return cell;
}

QString describeDay(const HeatmapDay &day, HeatMeasure measure)
{
    if (day.dictations == 0) {
        return QStringLiteral("No dictation");
    }
    const QString dictations = plural(day.dictations, QStringLiteral("dictation"));
    const QString words = plural(day.words, QStringLiteral("word"));
    switch (measure) {
    case HeatMeasure::Words:
        return QStringLiteral("%1 from %2").arg(words, dictations);
    case HeatMeasure::Audio:
        return QStringLiteral("%1 of audio").arg(duration((day.audioMs + 500) / 1000));
    case HeatMeasure::Dictations:
        break;
    }
    return QStringLiteral("%1, %2").arg(dictations, words);
}

// GitHub's layout for the latest `weeksShown` weeks: a column per
// Monday-first week, Mon/Wed/Fri on the left, month names over the week each
// month starts in.
Grid heatmapWeeks(const QList<HeatmapDay> &days, int weeksShown, HeatMeasure measure,
                  const PaneHost &host, const Brush &accent, const Brush &empty)
{
    const int first = static_cast<int>((days.size() + 6) / 7) - weeksShown;
    Grid grid;
    grid.ColumnSpacing(kHeatGap);
    grid.RowSpacing(kHeatGap);
    ColumnDefinition labelColumn;
    labelColumn.Width({kHeatLabelWidth, GridUnitType::Pixel});
    grid.ColumnDefinitions().Append(labelColumn);
    for (int week = 0; week < weeksShown; ++week) {
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
    const QMap<int, QString> months = monthLabels(days, weeksShown);
    for (auto label = months.cbegin(); label != months.cend(); ++label) {
        TextBlock month = secondaryCaption(label.value(), host);
        month.TextWrapping(TextWrapping::NoWrap);
        Grid::SetColumn(month, label.key() + 1);
        Grid::SetColumnSpan(month, std::min(4, weeksShown - label.key()));
        grid.Children().Append(month);
    }
    const HeatScale scale(days, measure);
    for (int index = first * 7; index < days.size(); ++index) {
        const HeatmapDay &day = days.at(index);
        Grid cell = hoverableCell(heatCell(scale.level(day), accent, empty));
        setImmediateTip(cell, describeDay(day, measure) + QLatin1Char('\n')
                                  + locale.toString(day.date, QStringLiteral("ddd, MMM d, yyyy")));
        Grid::SetColumn(cell, index / 7 - first + 1);
        Grid::SetRow(cell, index % 7 + 1);
        grid.Children().Append(cell);
    }
    return grid;
}

// The heatmap at whole weeks: as many of the latest as fit the card's width,
// refitted when it changes, the cells never shrinking.
UIElement heatmapGrid(const InsightsSummary &summary, HeatMeasure measure, const PaneHost &host,
                      const Brush &accent, const Brush &empty)
{
    Grid holder;
    auto shown = std::make_shared<int>(0);
    holder.SizeChanged([days = summary.heatmap, measure, &host, accent, empty, shown](
                           const IInspectable &sender, const SizeChangedEventArgs &args) {
        const int weeks = static_cast<int>((days.size() + 6) / 7);
        const int fitting = std::clamp(
            static_cast<int>((args.NewSize().Width - kHeatLabelWidth) / (kHeatCell + kHeatGap)), 1,
            weeks);
        if (fitting == *shown) {
            return;
        }
        *shown = fitting;
        const Grid target = sender.as<Grid>();
        target.Children().Clear();
        target.Children().Append(heatmapWeeks(days, fitting, measure, host, accent, empty));
    });
    return holder;
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
    body.Children().Append(heatmapGrid(summary, static_cast<HeatMeasure>(host.homeMeasure), host,
                                       accent, empty));

    StackPanel legend;
    legend.Orientation(Orientation::Horizontal);
    legend.Spacing(3);
    legend.Children().Append(secondaryCaption(QStringLiteral("Less"), host));
    for (int level = 0; level < static_cast<int>(kHeatStrengths.size()); ++level) {
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
        const double resting = hour == summary.peakHour ? 1.0 : kMutedBarOpacity;
        bar.Opacity(resting);
        setImmediateTip(bar, QStringLiteral("%1 to %2\n%3")
                                 .arg(hourLabel(hour), hourLabel((hour + 1) % 24),
                                      plural(count, QStringLiteral("dictation"))));
        // A hovered bar takes the full accent, as the peak does.
        bar.PointerEntered([bar](const IInspectable &, const auto &) { bar.Opacity(1.0); });
        bar.PointerExited([bar, resting](const IInspectable &, const auto &) { bar.Opacity(resting); });
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
    figures.Children().Append(figure(duration(qint64(summary.minutesSavedVersusTyping) * 60),
                                     QStringLiteral("Saved over typing"), host));
    body.Children().Append(figures);
    const double scale = std::max(summary.wordsPerMinute, 160);
    body.Children().Append(barTable(
        {{styledTextBlock(QStringLiteral("You, speaking"), L"SettingsCardBodyStyle"),
          double(summary.wordsPerMinute), scale, number(summary.wordsPerMinute), {}},
         {styledTextBlock(QStringLiteral("Typical typing"), L"SettingsCardBodyStyle"),
          double(summary.typingWordsPerMinute), scale, number(summary.typingWordsPerMinute), {}}},
        host));
    body.Children().Append(secondaryCaption(summary.speedupText, host));
    return cardContainer(body);
}

// A Writing Profile as a badge: its label on a pill of the chart accent at the
// heatmap's lightest level, as the Linux and macOS badges are.
Grid profileBadge(const QString &label, const Brush &accent)
{
    // The tint is its own layer so its opacity leaves the label at full
    // strength; the label's margin is the pill's padding.
    Border tint;
    tint.Background(accent);
    tint.Opacity(kHeatStrengths.at(1));
    tint.CornerRadius({9, 9, 9, 9});
    TextBlock text = styledTextBlock(label, L"CaptionTextBlockStyle");
    text.Margin({8, 1, 8, 2});
    Grid pill;
    pill.HorizontalAlignment(HorizontalAlignment::Left);
    pill.VerticalAlignment(VerticalAlignment::Center);
    pill.Children().Append(tint);
    pill.Children().Append(text);
    return pill;
}

UIElement appsCard(const InsightsSummary &summary, const PaneHost &host, const Brush &accent)
{
    if (summary.apps.isEmpty()) {
        return emptyPeriodCard(QStringLiteral("Where your words go"), host);
    }
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(QStringLiteral("Where your words go"), L"BodyStrongTextBlockStyle"));
    std::vector<BarEntry> entries;
    for (const AppShare &app : summary.apps) {
        StackPanel name;
        name.Orientation(Orientation::Horizontal);
        name.Spacing(8);
        TextBlock appName = styledTextBlock(app.name, L"SettingsCardBodyStyle");
        appName.VerticalAlignment(VerticalAlignment::Center);
        name.Children().Append(appName);
        if (!app.profileLabel.isEmpty()) {
            name.Children().Append(profileBadge(app.profileLabel, accent));
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
    open.Content(box_value(L"Review Corrections…"));
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
        append(QStringLiteral("Next milestone: %1 words").arg(number(summary.nextMilestone)),
               milestoneText(summary), progress);
    } else {
        append(QStringLiteral("Every milestone passed"), milestoneText(summary),
               value(plural(summary.allTimeWords, QStringLiteral("word"))));
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
           value(clockText(summary.longest.audioMs)));
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
            QStringLiteral("Speecher isn't recording new dictation. History you already have "
                           "stays on this computer until you clear it in Insights settings."),
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
    const ChartBrushes brushes = chartBrushes(host);
    const Brush &accent = brushes.accent;
    const Brush &empty = brushes.empty;

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
    insights.Children().Append(adaptiveRow({appsCard(summary, host, accent), correctionsCard(host)}, 320));
    column.Children().Append(insights);

    column.Children().Append(styledTextBlock(QStringLiteral("Records"), L"SettingsSectionHeaderStyle"));
    column.Children().Append(recordsCard(summary, today, host));
    column.Children().Append(privacyFooter(host));
    return scroll;
}

} // namespace speecher::win
