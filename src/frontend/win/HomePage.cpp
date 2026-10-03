#include "frontend/win/HomePage.h"

#include "app/ApplicationController.h"
#include "core/InsightsExport.h"
#include "core/InsightsLog.h"
#include "core/InsightsSummary.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "frontend/win/SettingsPage.h"
#include "frontend/win/WaveformBars.h"

#include <QClipboard>
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QSaveFile>
#include <QTimer>

#include <shobjidl.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma pop_macro("GetCurrentTime")

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Automation::AutomationProperties;
using winrt::Microsoft::UI::Xaml::Media::Brush;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
namespace Pickers = winrt::Windows::Storage::Pickers;

const QString kGeneralPage = QStringLiteral("general");
const QString kCorrectionsPage = QStringLiteral("vocabulary:corrections");
constexpr double kHeatCell = 12;
constexpr double kHeatGap = 3;
// Wide enough for a short weekday name in the caption style.
constexpr double kHeatLabelWidth = 32;
constexpr double kHourChartHeight = 80;
constexpr double kMutedBarOpacity = 0.42;

const std::array<InsightsRange, 4> kRanges{InsightsRange::Last7Days,
                                           InsightsRange::Last30Days,
                                           InsightsRange::ThisYear,
                                           InsightsRange::AllTime};

QString number(qint64 value)
{
    return QLocale().toString(value);
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

// A stat tile's icon (insightTileIconId). Segoe Fluent Icons has no flame, so
// the streak draws the Fluent UI System Icons fire (MIT, 16 px regular; see
// THIRD_PARTY_NOTICES.md) as a PathIcon in the caption's colour. The markup is
// parsed once into a template; each tile still needs its own PathIcon, since
// WinUI cannot share one Geometry between elements. The template is never
// released, so no XAML object outlives the XAML runtime at exit.
IconElement tileIcon(const QString &iconId)
{
    if (iconId == QStringLiteral("flame")) {
        static const auto *fire = new std::optional(winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(
            LR"(<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">)"
            LR"(<PathIcon Width="14" Height="14" Data="M8.1693 2.38161C8.44573 2.23863 8.72358 2.14217 8.9619 2.08199C8.98634 2.62292 9.15356 3.15614 9.38199 3.66553C9.70561 4.38719 10.1816 5.12315 10.6464 5.83569C10.6643 5.86313 10.6822 5.89054 10.7001 5.91792C11.1552 6.61534 11.5996 7.2963 11.9378 7.97958C12.2886 8.68856 12.5033 9.35877 12.5033 10C12.5033 11.1529 12.1583 12.1473 11.5113 12.8484C10.8703 13.5429 9.8868 14 8.50329 14C7.1062 14 6.13547 13.5958 5.457 12.9749C4.77036 12.3465 4.33504 11.4518 4.09918 10.3914C3.87699 9.39256 4.07292 8.49755 4.33365 7.84072C4.37607 7.73386 4.41999 7.63386 4.46377 7.54138L4.58929 7.79243C4.96992 8.55369 5.89562 8.86226 6.65688 8.48162C7.50305 8.05854 7.73716 7.02244 7.379 6.24601C7.00679 5.43912 6.71732 4.43545 6.97643 3.65811C7.17116 3.07394 7.63405 2.65846 8.1693 2.38161ZM4.11047 6.18914L4.1095 6.19037L4.10776 6.19258L4.1028 6.19894L4.08722 6.21936C4.07442 6.23635 4.0569 6.26009 4.0355 6.29026C3.99272 6.35055 3.93423 6.43679 3.8667 6.54649C3.73191 6.76546 3.5595 7.08054 3.40419 7.47178C3.09431 8.25245 2.84474 9.35744 3.12303 10.6086C3.38765 11.7982 3.89782 12.9035 4.78186 13.7126C5.67406 14.5292 6.89918 15 8.50329 15C10.121 15 11.3874 14.4571 12.2462 13.5266C13.0988 12.6027 13.5033 11.3471 13.5033 10C13.5033 9.14123 13.2178 8.31144 12.834 7.53604C12.4687 6.7979 11.9947 6.07168 11.5491 5.38919C11.5273 5.35579 11.5056 5.32249 11.484 5.2893C11.011 4.56435 10.5804 3.89406 10.2944 3.25635C10.0086 2.61894 9.89674 2.07627 9.99238 1.59806C10.0218 1.45117 9.98373 1.29885 9.88876 1.18301C9.79379 1.06716 9.65189 1 9.50209 1C9.08013 1 8.3769 1.14838 7.70988 1.49339C7.0368 1.84154 6.33303 2.42606 6.02775 3.34189C5.63764 4.51222 6.08674 5.83198 6.47095 6.66488C6.64198 7.03564 6.49348 7.44529 6.20967 7.5872C5.94238 7.72084 5.61736 7.6125 5.48372 7.34521L4.94931 6.27639C4.87293 6.12365 4.72388 6.02044 4.55403 6.0027C4.38417 5.98497 4.21676 6.05547 4.11047 6.18914Z"/></DataTemplate>)").as<DataTemplate>());
        return fire->value().LoadContent().as<IconElement>();
    }
    static const QHash<QString, wchar_t> glyphs{
        {QStringLiteral("text"), L'\uE8E4'},       // AlignLeft
        {QStringLiteral("microphone"), L'\uE720'}, // Microphone
        {QStringLiteral("waveform"), L'\uE8D6'},   // Audio
    };
    FontIcon icon;
    const wchar_t glyph = glyphs.value(iconId, L'\uE8E4');
    icon.Glyph(hstring(std::wstring_view(&glyph, 1)));
    icon.FontSize(14);
    return icon;
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
ComboBox indexPicker(const QStringList &labels,
                     int selected,
                     std::function<void(int)> choose)
{
    ComboBox combo;
    for (const QString &label : labels) {
        ComboBoxItem item;
        item.Content(box_value(hs(label)));
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

// The widest card's width with its text laid out unwrapped, and at least
// minWidth. Measured once per row: measured again later, a card answers with
// the layout it last had (a progress bar keeps its arranged width), so columns
// chosen from it flipped between two layouts until WinUI gave up with "Layout
// cycle detected".
double unwrappedWidth(const Grid &grid, double minWidth)
{
    double width = minWidth;
    for (const UIElement &card : grid.Children()) {
        card.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
        width = std::max(width, double(card.DesiredSize().Width));
    }
    // Measuring unconstrained left each card's desired size at its widest;
    // the grid measures them again at the column width.
    grid.InvalidateMeasure();
    return width;
}

// As many equal columns of at least columnWidth as fit and divide the cards
// evenly, so four tiles go 4, 2 or 1 across and a pair stacks when narrow.
void layoutColumns(const Grid &grid, double width, double columnWidth)
{
    const uint32_t count = grid.Children().Size();
    const double spacing = grid.ColumnSpacing();
    uint32_t columns = static_cast<uint32_t>(
        std::clamp(std::floor((width + spacing) / (columnWidth + spacing)), 1.0, double(count)));
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
    // Measured on the first layout in the window, where the cards' styles
    // apply, and kept: the same page always measures the same.
    auto columnWidth = std::make_shared<std::optional<double>>();
    grid.SizeChanged([minWidth, columnWidth](const IInspectable &sender, const SizeChangedEventArgs &args) {
        const Grid grid = sender.as<Grid>();
        if (!*columnWidth) {
            *columnWidth = unwrappedWidth(grid, minWidth);
        }
        layoutColumns(grid, args.NewSize().Width, **columnWidth);
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

// The copy button's glyph, or the check mark and "Copied" for a moment after.
void showCopied(const Button &copy, bool copied)
{
    FontIcon icon;
    icon.Glyph(copied ? L"\uE73E" : L"\uE8C8"); // CheckMark, Copy
    if (!copied) {
        copy.Content(icon);
        return;
    }
    StackPanel content;
    content.Orientation(Orientation::Horizontal);
    content.Spacing(6);
    content.Children().Append(icon);
    content.Children().Append(styledTextBlock(copiedCaption(), L"SettingsCardBodyStyle"));
    copy.Content(content);
}

UIElement dictationCard(PaneHost &host, const QDate &today)
{
    ApplicationController *controller = host.controller;
    StackPanel body = cardBody();

    // The status and hint on the left, the actions on the right, as a
    // settings row lays out its control.
    Grid top;
    top.ColumnSpacing(12);
    ColumnDefinition textColumn;
    textColumn.Width({1, GridUnitType::Star});
    ColumnDefinition toggleColumn;
    toggleColumn.Width({0, GridUnitType::Auto});
    top.ColumnDefinitions().Append(textColumn);
    top.ColumnDefinitions().Append(toggleColumn);
    StackPanel status;
    status.Spacing(2);
    status.VerticalAlignment(VerticalAlignment::Center);
    // The status, and while dictating the popup's waveform beside it: moving
    // while listening, flat and still in the caution colour while paused,
    // when the status says so in that colour too.
    QObject::disconnect(host.homeLevel);
    host.homeWaveform.reset();
    const SessionControls controls = sessionControls(controller->stateName());
    const bool listening = dictationListeningPresentation(controller->stateName());
    StackPanel heading;
    heading.Orientation(Orientation::Horizontal);
    heading.Spacing(12);
    TextBlock statusText = styledTextBlock(controller->statusLabel(), L"BodyStrongTextBlockStyle");
    statusText.VerticalAlignment(VerticalAlignment::Center);
    const Brush pausedFill = controls.paused ? themeBrush(L"PausedForeground", host) : Brush{nullptr};
    if (pausedFill) {
        statusText.Foreground(pausedFill);
    }
    heading.Children().Append(statusText);
    if (listening || controls.paused) {
        auto bars = std::make_shared<WaveformBars>();
        bars->element().Height(28);
        bars->setInk(statusText.Foreground());
        bars->setPaused(controls.paused, pausedFill);
        bars->setRunning(!controls.paused);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            bars->element(), hs(controls.paused ? controller->statusLabel() : inputLevelLabel()));
        host.homeLevel = QObject::connect(controller, &ApplicationController::audioLevelChanged,
                                          bars.get(), &WaveformBars::setLevel);
        heading.Children().Append(bars->element());
        host.homeWaveform = std::move(bars);
    }
    status.Children().Append(heading);
    const QString shortcut = controller->globalShortcutDisplay();
    status.Children().Append(secondaryCaption(dictationShortcutHint(shortcut), host));
    // The popup shows a failure for five seconds and cannot take focus, so the
    // reason also stays here until the next session starts.
    const QString failure =
        dictationFailureNote(controller->stateName(), controller->session()->lastFailure());
    if (!failure.isEmpty()) {
        TextBlock reason = styledTextBlock(failure, L"SettingsCardBodyStyle");
        reason.IsTextSelectionEnabled(true);
        status.Children().Append(reason);
    }
    top.Children().Append(status);

    // While dictating, Pause (Resume while paused) and Cancel join the toggle,
    // buttons like it with a glyph before the caption.
    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8);
    actions.VerticalAlignment(VerticalAlignment::Center);
    const auto actionButton = [](const wchar_t *glyphText, const QString &caption) {
        StackPanel content;
        content.Orientation(Orientation::Horizontal);
        content.Spacing(8);
        FontIcon icon;
        icon.Glyph(glyphText);
        icon.FontSize(14);
        content.Children().Append(icon);
        TextBlock label;
        label.Text(hs(caption));
        content.Children().Append(label);
        Button button;
        button.Content(content);
        button.VerticalAlignment(VerticalAlignment::Stretch);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, hs(caption));
        return button;
    };
    if (controls.pauseVisible) {
        Button pause = actionButton(controls.paused ? L"\uE768" : L"\uE769",
                                    controls.paused ? resumeCaption() : pauseCaption());
        pause.IsEnabled(controls.pauseEnabled);
        pause.Click([controller](const auto &, const auto &) {
            controller->session()->togglePause();
        });
        actions.Children().Append(pause);
    }
    if (controls.cancelVisible) {
        Button cancel = actionButton(L"\uE711", cancelCaption());
        cancel.Click([controller](const auto &, const auto &) { controller->cancel(); });
        actions.Children().Append(cancel);
    }
    const DictationToggleAction toggleAction = dictationToggleAction(controller->stateName());
    Button toggle;
    toggle.Content(box_value(hs(toggleAction.label)));
    toggle.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
    toggle.IsEnabled(toggleAction.enabled);
    toggle.Click([controller](const auto &, const auto &) { controller->toggle(); });
    actions.Children().Append(toggle);
    Grid::SetColumn(actions, 1);
    top.Children().Append(actions);
    body.Children().Append(top);

    const QString transcript = controller->session()->lastTranscript();
    if (transcript.isEmpty()) {
        return cardContainer(body);
    }
    QStringList meta{wordCountText(countWords(transcript))};
    if (const std::optional<DictationRecord> &record = controller->lastRecord()) {
        meta << record->appName << relativeDay(record->finishedAt.date(), today);
    }
    Grid last;
    last.ColumnSpacing(12);
    ColumnDefinition quoteColumn;
    quoteColumn.Width({1, GridUnitType::Star});
    ColumnDefinition copyColumn;
    copyColumn.Width({0, GridUnitType::Auto});
    last.ColumnDefinitions().Append(quoteColumn);
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
    showCopied(copy, false);
    copy.VerticalAlignment(VerticalAlignment::Top);
    ToolTipService::SetToolTip(copy, box_value(hs(copyTranscriptCaption())));
    copy.Click([transcript, weak = winrt::make_weak(copy)](const auto &, const auto &) {
        QGuiApplication::clipboard()->setText(transcript);
        if (const Button button = weak.get()) {
            showCopied(button, true);
        }
        QTimer::singleShot(kCopiedFeedbackMs, [weak] {
            if (const Button button = weak.get()) {
                showCopied(button, false);
            }
        });
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

// A chart as one stop for the keyboard and assistive tech: its marks have no
// text, so the stop carries the chart's title and core's description of it.
ContentControl describedChart(const UIElement &chart, const QString &title, const QString &description)
{
    ContentControl stop;
    stop.Content(chart);
    stop.HorizontalContentAlignment(HorizontalAlignment::Stretch);
    stop.IsTabStop(true);
    stop.UseSystemFocusVisuals(true);
    AutomationProperties::SetName(stop, hs(title));
    AutomationProperties::SetHelpText(stop, hs(description));
    return stop;
}

// A tile's icon and title, its figure, and the lines under it; the first
// line's tip becomes its tooltip.
StackPanel statTile(const InsightTileText &text, const PaneHost &host)
{
    StackPanel tile = cardBody();
    tile.Spacing(4);
    StackPanel title;
    title.Orientation(Orientation::Horizontal);
    title.Spacing(6);
    IconElement icon = tileIcon(text.iconId);
    if (const auto brush = themeBrush(L"SettingsCardDescriptionForeground", host)) {
        icon.Foreground(brush);
    }
    icon.VerticalAlignment(VerticalAlignment::Center);
    title.Children().Append(icon);
    title.Children().Append(secondaryCaption(text.title, host));
    tile.Children().Append(title);
    tile.Children().Append(styledTextBlock(
        text.unit.isEmpty() ? text.value : text.value + QLatin1Char(' ') + text.unit,
        L"SubtitleTextBlockStyle"));
    for (int index = 0; index < text.lines.size(); ++index) {
        TextBlock line = secondaryCaption(text.lines.at(index), host);
        if (index == 0 && !text.firstLineTip.isEmpty()) {
            ToolTipService::SetToolTip(line, box_value(hs(text.firstLineTip)));
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
        TextBlock letter = secondaryCaption(weekdayLetter(day + 1), host);
        letter.HorizontalAlignment(HorizontalAlignment::Center);
        column.Children().Append(letter);
        dots.Children().Append(column);
    }
    return dots;
}

UIElement statTiles(const InsightsSummary &summary, const QDate &today, const PaneHost &host,
                    const Brush &accent, const Brush &empty)
{
    std::vector<UIElement> tiles;
    for (const InsightTileText &text : insightTiles(summary, today)) {
        StackPanel tile = statTile(text, host);
        if (text.showsWeek) {
            tile.Children().Append(
                describedChart(weekDots(summary, accent, empty, host), text.title, weekDescription(summary)));
        }
        tiles.push_back(cardContainer(tile));
    }
    return adaptiveRow(tiles, 0);
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

    // Row 1 is Monday. Two rows tall, so the caption is not clipped to a cell.
    const std::array<QString, 7> rowLabels = heatmapRowLabels();
    for (int row = 0; row < 7; ++row) {
        if (rowLabels.at(row).isEmpty()) {
            continue;
        }
        TextBlock text = secondaryCaption(rowLabels.at(row), host);
        text.VerticalAlignment(VerticalAlignment::Top);
        Grid::SetRow(text, row + 1);
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
        const ChartTip tip = heatmapDayTip(day, measure);
        setImmediateTip(cell, tip.title + QLatin1Char('\n') + tip.detail);
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

// The active days on the left and the Less-to-More legend on the right.
Grid heatFooter(const InsightsSummary &summary, const PaneHost &host, const Brush &accent,
                const Brush &empty)
{
    StackPanel legend;
    legend.Orientation(Orientation::Horizontal);
    legend.Spacing(3);
    legend.Children().Append(secondaryCaption(heatLegendLessText(), host));
    for (int level = 0; level < static_cast<int>(kHeatStrengths.size()); ++level) {
        Border cell = heatCell(level, accent, empty);
        cell.VerticalAlignment(VerticalAlignment::Center);
        legend.Children().Append(cell);
    }
    legend.Children().Append(secondaryCaption(heatLegendMoreText(), host));
    return titledHeader(secondaryCaption(activeDaysLastYearText(summary.activeDaysLastYear), host), legend);
}

UIElement activityCard(const InsightsSummary &summary, PaneHost &host,
                       const Brush &accent, const Brush &empty)
{
    StackPanel body = cardBody();
    body.Spacing(12);
    const HeatMeasure measure = static_cast<HeatMeasure>(host.homeMeasure);
    body.Children().Append(titledHeader(
        styledTextBlock(homeText(HomeText::Activity), L"BodyStrongTextBlockStyle"),
        indexPicker({heatMeasureLabel(HeatMeasure::Dictations), heatMeasureLabel(HeatMeasure::Words),
                     heatMeasureLabel(HeatMeasure::Audio)},
                    host.homeMeasure,
                    [&host](int index) {
                        host.homeMeasure = index;
                        host.refresh();
                    })));
    body.Children().Append(describedChart(heatmapGrid(summary, measure, host, accent, empty),
                                          homeText(HomeText::Activity),
                                          heatmapDescription(summary, measure)));
    body.Children().Append(heatFooter(summary, host, accent, empty));
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
        const ChartTip tip = hourTip(hour, count);
        setImmediateTip(bar, tip.title + QLatin1Char('\n') + tip.detail);
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
    body.Children().Append(styledTextBlock(homeText(HomeText::WhenYouTalk), L"BodyStrongTextBlockStyle"));
    if (!summary.hasHourData) {
        body.Children().Append(secondaryCaption(homeText(HomeText::NoHourData), host));
        return cardContainer(body);
    }
    body.Children().Append(styledTextBlock(personaText(summary), L"BodyStrongTextBlockStyle"));
    body.Children().Append(secondaryCaption(peakText(summary), host));
    body.Children().Append(describedChart(hourChart(summary, host, accent), homeText(HomeText::WhenYouTalk),
                                          hourChartDescription(summary)));
    return cardContainer(body);
}

UIElement emptyPeriodCard(const QString &title, const PaneHost &host)
{
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(title, L"BodyStrongTextBlockStyle"));
    body.Children().Append(secondaryCaption(homeText(HomeText::NoDictationInPeriod), host));
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
        return emptyPeriodCard(homeText(HomeText::Pace), host);
    }
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(homeText(HomeText::Pace), L"BodyStrongTextBlockStyle"));
    StackPanel figures;
    figures.Orientation(Orientation::Horizontal);
    figures.Spacing(32);
    figures.Children().Append(figure(QStringLiteral("%1 wpm").arg(summary.wordsPerMinute),
                                     homeText(HomeText::SpeakingPace), host));
    figures.Children().Append(figure(minutesText(summary.minutesSavedVersusTyping),
                                     homeText(HomeText::SavedOverTyping), host));
    body.Children().Append(figures);
    const double scale = std::max<double>(summary.wordsPerMinute, 160);
    body.Children().Append(barTable(
        {{styledTextBlock(homeText(HomeText::YouSpeaking), L"SettingsCardBodyStyle"),
          double(summary.wordsPerMinute), scale, number(summary.wordsPerMinute), {}},
         {styledTextBlock(homeText(HomeText::TypicalTyping), L"SettingsCardBodyStyle"),
          double(summary.typingWordsPerMinute), scale, number(summary.typingWordsPerMinute), {}}},
        host));
    body.Children().Append(secondaryCaption(summary.speedupText, host));
    return cardContainer(body);
}

UIElement appsCard(const InsightsSummary &summary, const PaneHost &host, const Brush &accent)
{
    if (summary.apps.isEmpty()) {
        return emptyPeriodCard(homeText(HomeText::WhereYourWordsGo), host);
    }
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(homeText(HomeText::WhereYourWordsGo), L"BodyStrongTextBlockStyle"));
    std::vector<BarEntry> entries;
    for (const AppShare &app : summary.apps) {
        StackPanel name;
        name.Orientation(Orientation::Horizontal);
        name.Spacing(8);
        TextBlock appName = styledTextBlock(app.name, L"SettingsCardBodyStyle");
        appName.VerticalAlignment(VerticalAlignment::Center);
        name.Children().Append(appName);
        if (!app.profileLabel.isEmpty()) {
            name.Children().Append(badge(app.profileLabel, accent));
        }
        entries.push_back({name, double(app.words), double(summary.apps.first().words),
                           QStringLiteral("%1%").arg(app.percent),
                           [&app] {
                               const ChartTip tip = appTip(app);
                               return tip.title + QLatin1Char('\n') + tip.detail;
                           }()});
    }
    body.Children().Append(barTable(entries, host));
    return cardContainer(body);
}

UIElement correctionsCard(PaneHost &host)
{
    StackPanel body = cardBody();
    body.Children().Append(styledTextBlock(learnedCorrectionsTitle(), L"BodyStrongTextBlockStyle"));
    const int learned = static_cast<int>(host.controller->settings()->learnedCorrections().size());
    const bool learning = host.controller->settings()->snapshot().correctionLearningEnabled;
    const bool accessibility =
        !host.controller->accessibilitySupported() || host.controller->accessibilityEnabled();
    // None learned yet: the note says why, and a 0 would only repeat it.
    if (learned > 0) {
        body.Children().Append(figure(number(learned), learnedCorrectionsCaption(learned), host));
    }
    body.Children().Append(secondaryCaption(learnedCorrectionsNote(learned, learning, accessibility), host));
    const QString action = learnedCorrectionsAction(learned, learning);
    if (!action.isEmpty()) {
        Button open;
        open.Content(box_value(hs(action)));
        open.Click([&host](const auto &, const auto &) { host.showPage(kCorrectionsPage); });
        body.Children().Append(open);
    }
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
    for (const InsightRecordText &record : insightRecords(summary, today)) {
        if (record.milestoneBar) {
            ProgressBar progress;
            progress.Width(160);
            progress.Minimum(0);
            progress.Maximum(summary.nextMilestone);
            progress.Value(summary.allTimeWords);
            append(record.title, record.detail, progress);
        } else {
            append(record.title, record.detail, value(record.value));
        }
    }
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
    TextBlock text = secondaryCaption(homeText(HomeText::PrivacyNote), host);
    Grid::SetColumn(text, 1);
    footer.Children().Append(text);
    HyperlinkButton settings;
    settings.Content(box_value(hs(homeText(HomeText::InsightsSettings))));
    settings.Padding({0, 2, 0, 0});
    settings.Click([&host](const auto &, const auto &) { host.showPage(kGeneralPage); });
    Grid::SetRow(settings, 1);
    Grid::SetColumn(settings, 1);
    footer.Children().Append(settings);
    return footer;
}

// The picture "Copy image with stats" puts on the clipboard, as on Linux and
// macOS: the period's four figures over the year's heatmap, in Home's card on
// the window's solid background.
Border statsImage(const InsightsSummary &summary, InsightsRange range, HeatMeasure measure,
                  const PaneHost &host, const Brush &accent, const Brush &empty)
{
    StackPanel body = cardBody();
    body.Spacing(12);
    body.Children().Append(titledHeader(styledTextBlock(insightsImageTitle(), L"BodyStrongTextBlockStyle"),
                                        secondaryCaption(insightsRangeLabel(range), host)));
    StackPanel figures;
    figures.Orientation(Orientation::Horizontal);
    figures.Spacing(32);
    // The tiles' figures, in their order and under their titles.
    for (const InsightTileText &tile : insightTiles(summary, QDate())) {
        StackPanel column;
        column.Children().Append(secondaryCaption(tile.title, host));
        column.Children().Append(styledTextBlock(
            tile.unit.isEmpty() ? tile.value : tile.value + QLatin1Char(' ') + tile.unit,
            L"SubtitleTextBlockStyle"));
        figures.Children().Append(column);
    }
    body.Children().Append(figures);
    if (const QString pace = insightsImagePaceLine(summary); !pace.isEmpty()) {
        body.Children().Append(secondaryCaption(pace, host));
    }
    body.Children().Append(heatmapWeeks(summary.heatmap, static_cast<int>((summary.heatmap.size() + 6) / 7),
                                        measure, host, accent, empty));
    body.Children().Append(heatFooter(summary, host, accent, empty));
    Border image;
    image.Background(themeBrush(L"StatsImageBackground", host));
    image.Padding({24, 24, 24, 24});
    image.Child(cardContainer(body));
    return image;
}

using WeakButton = winrt::weak_ref<Button>;

winrt::fire_and_forget saveJson(PaneHost &host, WeakButton button, QByteArray json, QDate today);

// The button's text says what the last choice did, then goes back.
void reportShare(const WeakButton &weak, const QString &text, const QString &tip = QString())
{
    const Button button = weak.get();
    if (!button) {
        return;
    }
    button.Content(box_value(hs(text)));
    if (!tip.isEmpty()) {
        ToolTipService::SetToolTip(button, box_value(hs(tip)));
    }
    QTimer::singleShot(tip.isEmpty() ? kCopiedFeedbackMs : 5000, [weak] {
        if (const Button button = weak.get()) {
            button.Content(box_value(hs(insightsShareLabels().share)));
            ToolTipService::SetToolTip(button, nullptr);
        }
    });
}

// Share: the stats as an image or text on the clipboard, or saved as JSON.
UIElement shareButton(PaneHost &host, const InsightsSummary &summary, InsightsRange range,
                      const QDate &today)
{
    const InsightsShareLabels labels = insightsShareLabels();
    DropDownButton button;
    button.Content(box_value(hs(labels.share)));
    MenuFlyout menu;
    menu.Placement(Primitives::FlyoutPlacementMode::BottomEdgeAlignedRight);
    const WeakButton weak = winrt::make_weak(button.as<Button>());
    MenuFlyoutItem copyImage;
    copyImage.Text(hs(labels.copyImage));
    FontIcon imageIcon;
    imageIcon.Glyph(L"\uEB9F"); // Photo
    copyImage.Icon(imageIcon);
    copyImage.Click([&host, weak, labels](const auto &, const auto &) {
        copyStatsImage(host, [weak, labels](bool copied) {
            if (copied) {
                reportShare(weak, labels.copied);
            }
        });
    });
    menu.Items().Append(copyImage);
    MenuFlyoutItem copyText;
    copyText.Text(hs(labels.copyText));
    FontIcon copyIcon;
    copyIcon.Glyph(L"\uE8C8");
    copyText.Icon(copyIcon);
    const QString text = insightsShareText(summary, range);
    copyText.Click([weak, text, labels](const auto &, const auto &) {
        QGuiApplication::clipboard()->setText(text);
        reportShare(weak, labels.copied);
    });
    menu.Items().Append(copyText);
    menu.Items().Append(MenuFlyoutSeparator());
    MenuFlyoutItem save;
    save.Text(hs(labels.saveJson));
    FontIcon saveIcon;
    saveIcon.Glyph(L"\uE792"); // SaveAs
    save.Icon(saveIcon);
    const QByteArray json = insightsJson(summary, range, today);
    save.Click([&host, weak, json, today](const auto &, const auto &) {
        saveJson(host, weak, json, today);
    });
    menu.Items().Append(save);
    button.Flyout(menu);
    return button;
}

winrt::fire_and_forget saveJson(PaneHost &host, WeakButton button, QByteArray json, QDate today)
{
    const std::weak_ptr<bool> alive = host.alive;
    const InsightsShareLabels labels = insightsShareLabels();
    try {
        Pickers::FileSavePicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(host.hwnd()));
        picker.SuggestedStartLocation(Pickers::PickerLocationId::DocumentsLibrary);
        picker.SuggestedFileName(hs(QFileInfo(insightsJsonFileName(today)).completeBaseName()));
        picker.FileTypeChoices().Insert(hs(labels.jsonFilter),
                                        winrt::single_threaded_vector<hstring>({hstring(L".json")}));
        const auto file = co_await picker.PickSaveFileAsync();
        if (gone(alive) || !file) {
            co_return;
        }
        QSaveFile out(qs(file.Path()));
        const bool saved = out.open(QIODevice::WriteOnly) && out.write(json) >= 0 && out.commit();
        if (saved) {
            reportShare(button, labels.saved);
        } else {
            reportShare(button, labels.saveFailed, out.errorString());
        }
    } catch (const winrt::hresult_error &error) {
        qWarning() << "saving the stats failed:" << qs(error.message());
    }
}

} // namespace

winrt::fire_and_forget copyStatsImage(PaneHost &host, std::function<void(bool copied)> done)
{
    using winrt::Microsoft::UI::Xaml::Media::Imaging::RenderTargetBitmap;
    const std::weak_ptr<bool> alive = host.alive;
    const auto root = host.xamlRoot ? host.xamlRoot() : winrt::Microsoft::UI::Xaml::XamlRoot{nullptr};
    const auto window = root ? root.Content().try_as<Panel>() : nullptr;
    if (!window) {
        done(false);
        co_return;
    }
    // RenderTargetBitmap draws only what is in the window's tree, but not
    // what is on screen: a canvas lays the picture out at its own size, then
    // holds it far to the left of the window.
    Canvas stage;
    bool copied = false;
    try {
        const QList<DictationRecord> &records = host.controller->insightsLog()->records();
        const InsightsSummary summary = summarize(records, host.homeRange, host.controller->insightsToday(),
                                                  host.controller->settings()->writingProfileSettings());
        const ChartBrushes brushes = chartBrushes(host);
        const Border image = statsImage(summary, host.homeRange, static_cast<HeatMeasure>(host.homeMeasure),
                                        host, brushes.accent, brushes.empty);
        stage.IsHitTestVisible(false);
        stage.Children().Append(image);
        window.Children().Append(stage);
        image.UpdateLayout();
        const winrt::Windows::Foundation::Size size = image.DesiredSize();
        // Twice the layout size in pixels, as on Linux, so it stays sharp when
        // pasted. RenderTargetBitmap draws at the window's rasterization scale
        // and stretches anything bigger, so a Viewbox lays the picture out
        // that much larger instead and the text is drawn at that size.
        const double scale = 2 / root.RasterizationScale();
        stage.Children().Clear();
        Viewbox enlarged;
        enlarged.Width(std::ceil(size.Width * scale));
        enlarged.Height(std::ceil(size.Height * scale));
        enlarged.Child(image);
        Canvas::SetLeft(enlarged, -100000);
        stage.Children().Append(enlarged);
        enlarged.UpdateLayout();
        RenderTargetBitmap bitmap;
        co_await bitmap.RenderAsync(enlarged);
        const auto pixels = co_await bitmap.GetPixelsAsync();
        if (!gone(alive)) {
            // BGRA8, premultiplied: QImage's ARGB32 on a little-endian machine.
            QGuiApplication::clipboard()->setImage(QImage(pixels.data(), bitmap.PixelWidth(), bitmap.PixelHeight(),
                                                          bitmap.PixelWidth() * 4,
                                                          QImage::Format_ARGB32_Premultiplied)
                                                       .copy());
            copied = true;
        }
    } catch (const winrt::hresult_error &error) {
        qWarning() << "drawing the stats image failed:" << qs(error.message());
    }
    if (!gone(alive)) {
        uint32_t index = 0;
        if (window.Children().IndexOf(stage, index)) {
            window.Children().RemoveAt(index);
        }
    }
    done(copied);
}

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
        StackPanel off = notice(homeText(HomeText::InsightsOffTitle), homeText(HomeText::InsightsOffBody), host);
        Button settings;
        settings.Content(box_value(hs(homeText(HomeText::InsightsSettings))));
        settings.Click([&host](const auto &, const auto &) { host.showPage(kGeneralPage); });
        off.Children().Append(settings);
        cards.Children().Append(cardContainer(off));
        return scroll;
    }
    if (records.isEmpty()) {
        cards.Children().Append(cardContainer(
            notice(homeText(HomeText::NoInsightsTitle), homeText(HomeText::NoInsightsBody), host)));
        return scroll;
    }

    const InsightsSummary summary =
        summarize(records, host.homeRange, today, controller->settings()->writingProfileSettings());
    const ChartBrushes brushes = chartBrushes(host);
    const Brush &accent = brushes.accent;
    const Brush &empty = brushes.empty;

    const int rangeIndex = static_cast<int>(
        std::find(kRanges.begin(), kRanges.end(), host.homeRange) - kRanges.begin());
    StackPanel pickers;
    pickers.Orientation(Orientation::Horizontal);
    pickers.Spacing(8);
    QStringList ranges;
    for (const InsightsRange range : kRanges) {
        ranges.append(insightsRangeLabel(range));
    }
    pickers.Children().Append(indexPicker(ranges,
                                          rangeIndex,
                                          [&host](int index) {
                                              host.homeRange = kRanges.at(index);
                                              host.refresh();
                                          }));
    pickers.Children().Append(shareButton(host, summary, host.homeRange, today));
    column.Children().Append(titledHeader(
        styledTextBlock(homeText(HomeText::YourDictation), L"SettingsSectionHeaderStyle"), pickers));

    StackPanel insights;
    insights.Spacing(4);
    insights.Children().Append(statTiles(summary, today, host, accent, empty));
    insights.Children().Append(activityCard(summary, host, accent, empty));
    insights.Children().Append(adaptiveRow({hoursCard(summary, host, accent), paceCard(summary, host)}, 320));
    insights.Children().Append(adaptiveRow({appsCard(summary, host, accent), correctionsCard(host)}, 320));
    column.Children().Append(insights);

    column.Children().Append(styledTextBlock(homeText(HomeText::Records), L"SettingsSectionHeaderStyle"));
    column.Children().Append(recordsCard(summary, today, host));
    column.Children().Append(privacyFooter(host));
    return scroll;
}

} // namespace speecher::win
