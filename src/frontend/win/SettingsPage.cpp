#include "frontend/win/SettingsPage.h"

#include "frontend/win/CollectionEditor.h"
#include "frontend/win/CustomRows.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/ShortcutRecorder.h"

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <algorithm>
#include <cmath>

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Markup::XamlReader;

constexpr auto kXmlns =
    LR"(xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation")";

Style lookupStyle(const wchar_t *key)
{
    return Application::Current()
        .Resources()
        .Lookup(box_value(key))
        .as<Style>();
}

TextBlock styledText(const QString &text, const wchar_t *styleKey)
{
    return styledTextBlock(text, styleKey);
}

// The room a Choice or Text control reserves, from the schema's width hint in
// characters, so a list that arrives late does not resize the row.
double contentMinWidth(const RowSnapshot &row)
{
    return std::max(120.0, row.contentWidthHint * 8.0);
}

} // namespace

ComboBox choiceComboBox(const RowSnapshot &row, PaneHost &host)
{
    ComboBox combo;
    combo.MinWidth(contentMinWidth(row));
    const QString current = row.value.toString();
    int selected = -1;
    bool anyEnabled = false;
    for (const RowOption &option : row.options) {
        ComboBoxItem item;
        item.Content(box_value(hs(option.label)));
        item.Tag(box_value(hs(option.id)));
        item.IsEnabled(option.enabled);
        anyEnabled = anyEnabled || option.enabled;
        if (!option.help.isEmpty()) {
            ToolTipService::SetToolTip(item, box_value(hs(option.help)));
        }
        if (option.id == current) {
            selected = combo.Items().Size();
        }
        combo.Items().Append(item);
    }
    combo.SelectedIndex(selected);
    if (!row.options.isEmpty() && !anyEnabled) {
        combo.IsEnabled(false);
    }
    combo.SelectionChanged([rowId = row.id, &host](const IInspectable &sender, const auto &) {
        const auto item = sender.as<ComboBox>().SelectedItem();
        if (!item) {
            return;
        }
        setValueAndCommit(host, rowId, qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag())));
    });
    return combo;
}

namespace {

ToggleSwitch toggleSwitch(const RowSnapshot &row, PaneHost &host)
{
    ToggleSwitch toggle;
    // The Settings app's right-aligned switch says nothing beside itself; the
    // template otherwise reserves a 154 px label column.
    toggle.OnContent(box_value(L""));
    toggle.OffContent(box_value(L""));
    toggle.MinWidth(0);
    toggle.IsOn(row.value.toBool());
    toggle.Toggled([rowId = row.id, &host](const IInspectable &sender, const auto &) {
        setValueAndCommit(host, rowId, sender.as<ToggleSwitch>().IsOn());
    });
    return toggle;
}

UIElement textField(const RowSnapshot &row, PaneHost &host)
{
    if (row.suggestions.isEmpty()) {
        TextBox box;
        box.MinWidth(contentMinWidth(row));
        box.Text(hs(row.value.toString()));
        const auto commit = [rowId = row.id, stored = row.value.toString(), &host](
                                const TextBox &box) {
            const QString text = qs(box.Text());
            if (text != stored) {
                setValueAndCommit(host, rowId, text);
            }
        };
        box.LostFocus([commit](const IInspectable &sender, const auto &) {
            commit(sender.as<TextBox>());
        });
        box.KeyDown([commit](const IInspectable &sender, const Input::KeyRoutedEventArgs &args) {
            if (args.Key() == Windows::System::VirtualKey::Enter) {
                commit(sender.as<TextBox>());
            }
        });
        return box;
    }
    // A row that names values worth offering gets the suggestion box, though it
    // still takes any text a person types.
    AutoSuggestBox box;
    box.MinWidth(contentMinWidth(row));
    box.Text(hs(row.value.toString()));
    QStringList suggestions;
    for (const RowOption &option : row.suggestions) {
        suggestions.append(option.id);
    }
    box.TextChanged([suggestions](const AutoSuggestBox &sender,
                                  const AutoSuggestBoxTextChangedEventArgs &args) {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) {
            return;
        }
        const QString needle = qs(sender.Text()).toLower();
        auto matches = winrt::single_threaded_vector<IInspectable>();
        for (const QString &suggestion : suggestions) {
            if (suggestion.toLower().contains(needle)) {
                matches.Append(box_value(hs(suggestion)));
            }
        }
        sender.ItemsSource(matches);
    });
    const auto commit = [rowId = row.id, stored = row.value.toString(), &host](
                            const AutoSuggestBox &box) {
        const QString text = qs(box.Text());
        if (text != stored) {
            setValueAndCommit(host, rowId, text);
        }
    };
    box.QuerySubmitted([commit](const AutoSuggestBox &sender, const auto &) { commit(sender); });
    box.LostFocus([commit](const IInspectable &sender, const auto &) {
        commit(sender.as<AutoSuggestBox>());
    });
    return box;
}

UIElement numberField(const RowSnapshot &row, PaneHost &host)
{
    NumberBox box;
    box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
    box.Minimum(std::min(row.range.minimum, row.range.maximum));
    box.Maximum(std::max(row.range.minimum, row.range.maximum));
    box.SmallChange(row.range.step);
    box.LargeChange(row.range.step);
    box.Value(row.value.toInt());
    box.ValueChanged([rowId = row.id, stored = row.value.toInt(), &host](
                         const NumberBox &sender, const NumberBoxValueChangedEventArgs &args) {
        // Cleared rather than changed: put the stored value back.
        if (std::isnan(args.NewValue())) {
            sender.Value(stored);
            return;
        }
        const int value = static_cast<int>(args.NewValue());
        if (value != stored) {
            setValueAndCommit(host, rowId, value);
        }
    });
    if (row.range.suffix.isEmpty()) {
        return box;
    }
    StackPanel panel;
    panel.Orientation(Orientation::Horizontal);
    panel.Spacing(8);
    panel.Children().Append(box);
    TextBlock suffix = secondaryTextBlock(row.range.suffix.trimmed(), L"SettingsInfoTextStyle", host);
    suffix.VerticalAlignment(VerticalAlignment::Center);
    panel.Children().Append(suffix);
    return panel;
}

Button actionButton(const RowSnapshot &row, PaneHost &host)
{
    Button button;
    button.Content(box_value(hs(row.actionLabel)));
    button.Click([rowId = row.id, &host](const auto &, const auto &) {
        if (host.action) {
            host.action(rowId);
        }
    });
    return button;
}

// The control a row's kind asks for; null for the rows the card itself hosts
// full width (collections, the profile grid, the release notes).
UIElement rowControl(const RowSnapshot &row, PaneHost &host)
{
    switch (row.kind) {
    case RowKind::Choice:
        return choiceComboBox(row, host);
    case RowKind::Toggle:
        return toggleSwitch(row, host);
    case RowKind::Text:
        return textField(row, host);
    case RowKind::Number:
        return numberField(row, host);
    case RowKind::Action:
        return actionButton(row, host);
    case RowKind::Info:
        return secondaryTextBlock(row.value.toString(), L"SettingsInfoTextStyle", host);
    case RowKind::Collection:
        return nullptr;
    case RowKind::Custom:
        return customRowIsFullWidth(row.id) ? nullptr : customRowElement(row, host);
    }
    return nullptr;
}

// The one editor per collection row, created once and kept on the host so a
// pane rebuild cannot drop the undo history or a half-typed cell.
std::shared_ptr<CollectionEditor> editorFor(const RowSnapshot &row, PaneHost &host)
{
    auto editor = host.editors.value(row.id);
    if (!editor) {
        editor = std::make_shared<CollectionEditor>(row, host);
        host.editors.insert(row.id, editor);
    }
    return editor;
}

// The footnote under a section's cards: the schema section's help, or the help
// of a collection row filling the card, which has nowhere else to put it.
QString footnote(const SectionSnapshot &section)
{
    if (!section.help.isEmpty()) {
        return section.help;
    }
    for (const RowSnapshot &row : section.rows) {
        if (row.collection) {
            return row.help;
        }
    }
    return {};
}

// One schema section as the Settings app draws it: a BodyStrong header, one
// SettingsCard per row — rows sharing a groupId in one card — spaced 4, and
// the footnote underneath.
void appendSection(const StackPanel &column, const SectionSnapshot &section, PaneHost &host)
{
    if (section.rows.isEmpty()) {
        return;
    }
    if (!section.title.isEmpty()) {
        column.Children().Append(styledText(section.title, L"SettingsSectionHeaderStyle"));
    }
    StackPanel cards;
    cards.Spacing(4);
    if (section.title.isEmpty()) {
        cards.Margin({0, 12, 0, 0});
    }
    // Consecutive rows naming the same group share one card; a collection or
    // full-width custom row is always a card of its own.
    QList<QList<RowSnapshot>> units;
    for (const RowSnapshot &row : section.rows) {
        const bool standsAlone = row.kind == RowKind::Collection
            || (row.kind == RowKind::Custom && customRowIsFullWidth(row.id));
        if (!standsAlone && !units.isEmpty() && !row.groupId.isEmpty()
            && units.last().last().groupId == row.groupId
            && units.last().last().kind != RowKind::Collection) {
            units.last().append(row);
            continue;
        }
        units.append({row});
    }
    for (const QList<RowSnapshot> &unit : units) {
        const RowSnapshot &first = unit.first();
        if (first.kind == RowKind::Collection) {
            cards.Children().Append(editorFor(first, host)->card());
            continue;
        }
        if (first.kind == RowKind::Custom && customRowIsFullWidth(first.id)) {
            cards.Children().Append(cardContainer(customRowElement(first, host)));
            continue;
        }
        StackPanel rows;
        for (qsizetype index = 0; index < unit.size(); ++index) {
            rows.Children().Append(rowGrid(unit.at(index),
                                           rowControl(unit.at(index), host),
                                           host,
                                           index > 0));
        }
        cards.Children().Append(cardContainer(rows));
    }
    column.Children().Append(cards);
    const QString help = footnote(section);
    if (!help.isEmpty()) {
        column.Children().Append(secondaryTextBlock(help, L"SettingsFootnoteStyle", host));
    }
}

// The Gallery's settings page scaffold: gutters on the scroller, the column
// capped at 1064 inside them, the page title on top.
ScrollViewer pageScaffold(const QString &title, StackPanel &column)
{
    ScrollViewer scroll;
    scroll.Padding({36, 0, 36, 0});
    column.MaxWidth(1064);
    column.Padding({0, 0, 0, 36});
    column.Children().Append(styledText(title, L"SettingsPageTitleStyle"));
    scroll.Content(column);
    return scroll;
}

} // namespace

TextBlock styledTextBlock(const QString &text, const wchar_t *styleKey)
{
    TextBlock block;
    block.Style(lookupStyle(styleKey));
    block.Text(hs(text));
    return block;
}

TextBlock secondaryTextBlock(const QString &text, const wchar_t *styleKey, const PaneHost &host)
{
    TextBlock block = styledTextBlock(text, styleKey);
    const ElementTheme theme = host.effectiveTheme ? host.effectiveTheme()
                                                   : ElementTheme::Default;
    const hstring themeKey = theme == ElementTheme::Light ? L"Light" : L"Dark";
    // The style dictionary is the merged dictionary that carries our theme
    // dictionaries; walk the merged list rather than assuming its position.
    for (const auto &merged : Application::Current().Resources().MergedDictionaries()) {
        const auto themes = merged.ThemeDictionaries();
        if (!themes.HasKey(box_value(themeKey))) {
            continue;
        }
        const auto dictionary = themes.Lookup(box_value(themeKey)).as<ResourceDictionary>();
        if (const auto brush = dictionary.TryLookup(
                box_value(L"SettingsCardDescriptionForeground"))) {
            block.Foreground(brush.as<winrt::Microsoft::UI::Xaml::Media::Brush>());
        }
        break;
    }
    return block;
}

void detachFromParent(const UIElement &element)
{
    if (!element) {
        return;
    }
    // The logical parent, not VisualTreeHelper: a discarded pane that never
    // reached the live visual tree has no visual parent, but its panel still
    // holds the element and appending it elsewhere throws "already the child
    // of another element".
    const auto framework = element.try_as<FrameworkElement>();
    const auto parent = framework ? framework.Parent() : nullptr;
    if (!parent) {
        return;
    }
    if (auto panel = parent.try_as<Panel>()) {
        uint32_t index = 0;
        if (panel.Children().IndexOf(element, index)) {
            panel.Children().RemoveAt(index);
        }
    } else if (auto border = parent.try_as<Border>()) {
        border.Child(nullptr);
    }
}

Border cardContainer(const UIElement &content)
{
    static const hstring xaml = hstring(L"<Border ") + kXmlns
        + LR"( Background="{ThemeResource SettingsCardBackground}")"
        + LR"( BorderBrush="{ThemeResource SettingsCardBorderBrush}")"
        + LR"( BorderThickness="1" CornerRadius="{ThemeResource ControlCornerRadius}"/>)";
    Border card = XamlReader::Load(xaml).as<Border>();
    detachFromParent(content);
    card.Child(content);
    return card;
}

Grid rowGrid(const RowSnapshot &row, const UIElement &control, PaneHost &host, bool followsRow)
{
    Grid grid;
    if (followsRow) {
        // The inset separator grouped rows share, drawn by the card's own
        // stroke so it matches in every theme.
        static const hstring xaml = hstring(L"<Grid ") + kXmlns
            + LR"( BorderThickness="0,1,0,0" BorderBrush="{ThemeResource SettingsCardBorderBrush}"/>)";
        grid = XamlReader::Load(xaml).as<Grid>();
    }
    grid.MinHeight(68);
    grid.Padding({16, 16, 16, 16});
    grid.ColumnSpacing(16);
    ColumnDefinition labelColumn;
    labelColumn.Width({1, GridUnitType::Star});
    ColumnDefinition contentColumn;
    contentColumn.Width({0, GridUnitType::Auto});
    grid.ColumnDefinitions().Append(labelColumn);
    grid.ColumnDefinitions().Append(contentColumn);

    StackPanel header;
    header.VerticalAlignment(VerticalAlignment::Center);
    header.Spacing(2);
    if (!row.label.isEmpty()) {
        header.Children().Append(styledText(row.label, L"SettingsCardBodyStyle"));
    }
    // disabledHelp replaces the description while enabled says no.
    const QString description = row.enabled ? row.help : row.disabledHelp;
    if (!description.isEmpty()) {
        header.Children().Append(secondaryTextBlock(description, L"SettingsCardDescriptionStyle", host));
    }
    if (!row.enabled && !row.disabledAction.isEmpty()) {
        HyperlinkButton lift;
        lift.Content(box_value(hs(row.disabledActionLabel)));
        lift.Padding({0, 2, 0, 0});
        lift.Click([actionId = row.disabledAction, &host](const auto &, const auto &) {
            if (host.action) {
                host.action(actionId);
            }
        });
        header.Children().Append(lift);
    }
    grid.Children().Append(header);

    if (control) {
        if (auto element = control.try_as<FrameworkElement>()) {
            element.VerticalAlignment(VerticalAlignment::Center);
            element.HorizontalAlignment(HorizontalAlignment::Right);
        }
        if (auto element = control.try_as<Control>()) {
            if (!row.enabled) {
                element.IsEnabled(false);
            }
        }
        if (row.enabled && !row.tooltip.isEmpty()) {
            ToolTipService::SetToolTip(control, box_value(hs(row.tooltip)));
        }
        Grid::SetColumn(control.as<FrameworkElement>(), 1);
        grid.Children().Append(control);
    }
    return grid;
}

void setValueAndCommit(PaneHost &host, const QString &rowId, const QVariant &value)
{
    host.model->setValue(rowId, value);
    host.model->commit();
    if (host.refresh) {
        host.refresh();
    }
}

UIElement buildPage(const PageSnapshot &page, PaneHost &host)
{
    StackPanel column;
    ScrollViewer scroll = pageScaffold(page.title, column);
    for (const SectionSnapshot &section : page.sections) {
        appendSection(column, section, host);
    }
    return scroll;
}

UIElement buildShortcutPage(PaneHost &host)
{
    StackPanel column;
    ScrollViewer scroll = pageScaffold(QStringLiteral("Shortcut"), column);
    ShortcutRecorder::appendPane(column, host);
    return scroll;
}

} // namespace speecher::win
