#include "frontend/win/CollectionEditor.h"

#include "frontend/win/SettingsModel.h"

#include <QFile>

#include <algorithm>

#include <shobjidl.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Automation::AutomationProperties;

const QString kUndoDelete = QStringLiteral("undoDelete");
const QString kUndoLatestLearn = QStringLiteral("undoLatestLearn");

// How much of the list's width a column takes. The descriptor names the one
// that takes the leftover; a flag needs no more than its checkbox, and the
// rest get widths their values fit in. Columns that only show text share the
// leftover in a narrow window, up to that width, so a table of six columns
// still leaves the leftover one room.
ColumnDefinition columnDefinition(const CollectionColumnSnapshot &column)
{
    ColumnDefinition definition;
    if (column.stretch) {
        definition.Width({1, GridUnitType::Star});
        return definition;
    }
    switch (column.kind) {
    case ColumnKind::Toggle:
        definition.Width({56, GridUnitType::Pixel});
        break;
    case ColumnKind::Choice:
        definition.Width({150, GridUnitType::Pixel});
        break;
    case ColumnKind::Text:
        definition.Width({140, GridUnitType::Pixel});
        break;
    case ColumnKind::ReadOnly:
        definition.Width({0.4, GridUnitType::Star});
        definition.MaxWidth(110);
        break;
    case ColumnKind::ChoiceSet:
        definition.Width({0.55, GridUnitType::Star});
        definition.MaxWidth(150);
        break;
    }
    return definition;
}

Grid columnGrid(const QList<CollectionColumnSnapshot> &columns)
{
    Grid grid;
    grid.ColumnSpacing(8);
    for (const CollectionColumnSnapshot &column : columns) {
        grid.ColumnDefinitions().Append(columnDefinition(column));
    }
    return grid;
}

// The columns the table shows. A dialog-only column is a field of the record
// dialog instead, where that dialog can edit a record; where it can only add
// one, the table stays the place to change the column afterwards.
QList<CollectionColumnSnapshot> tableColumns(const CollectionSnapshot &collection)
{
    QList<CollectionColumnSnapshot> shown;
    for (const CollectionColumnSnapshot &column : collection.columns) {
        if (!column.dialogOnly || collection.editLabel.isEmpty()) {
            shown.append(column);
        }
    }
    return shown;
}

// What a record is called, such as a vocabulary term: its first text column.
QString recordName(const CollectionSnapshot &collection, const QVariantMap &record)
{
    for (const CollectionColumnSnapshot &column : collection.columns) {
        if (column.kind == ColumnKind::Text) {
            return record.value(column.id).toString();
        }
    }
    return {};
}

TextBlock cellText(const QString &text,
                   const wchar_t *styleKey,
                   const PaneHost *secondaryOf)
{
    TextBlock block = secondaryOf ? secondaryTextBlock(text, styleKey, *secondaryOf)
                                  : styledTextBlock(text, styleKey);
    block.TextTrimming(TextTrimming::CharacterEllipsis);
    block.TextWrapping(TextWrapping::NoWrap);
    block.VerticalAlignment(VerticalAlignment::Center);
    return block;
}

/// What a cell nobody may edit says: a choice shows the label behind the
/// stored id, and a flag reads as a word rather than an empty checkbox.
QString displayText(const CollectionColumnSnapshot &column, const QVariant &value)
{
    if (column.kind == ColumnKind::Choice) {
        for (const RowOption &option : column.options) {
            if (option.id == value.toString()) {
                return option.label;
            }
        }
    }
    if (column.kind == ColumnKind::Toggle) {
        return value.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return value.toString();
}

// A pill in one of the rating badges' tones, named for screen readers.
Grid namedBadge(const QString &text, const wchar_t *brushKey, const PaneHost &host)
{
    Grid pill = badge(text, themeBrush(brushKey, host));
    AutomationProperties::SetName(pill, hs(text));
    return pill;
}

// A cell and a pill on one line. A Grid, not a horizontal StackPanel, so the
// cell still fills the column and the pill takes only its own width.
Grid withBadge(const UIElement &cell, const Grid &pill, bool pillFirst)
{
    Grid line;
    line.ColumnSpacing(8);
    ColumnDefinition field;
    field.Width({1, GridUnitType::Star});
    ColumnDefinition label;
    label.Width({0, GridUnitType::Auto});
    line.ColumnDefinitions().Append(pillFirst ? label : field);
    line.ColumnDefinitions().Append(pillFirst ? field : label);
    Grid::SetColumn(cell.as<FrameworkElement>(), pillFirst ? 1 : 0);
    Grid::SetColumn(pill, pillFirst ? 0 : 1);
    line.Children().Append(cell);
    line.Children().Append(pill);
    return line;
}

} // namespace

CollectionEditor::CollectionEditor(const RowSnapshot &row, PaneHost &host)
    : m_rowId(row.id)
    , m_collection(*row.collection)
    , m_host(host)
{
    // The settings' records, taken once: from here on the editor's copy is the
    // live one, so a rebuild cannot undo an edit.
    const QList<QVariantMap> stored = row.value.value<QList<QVariantMap>>();
    for (qsizetype index = 0; index < stored.size(); ++index) {
        m_records.append({stored.at(index), index < m_collection.lockedRecordCount});
    }
    m_savedRecords = editableRecords();
}

UIElement CollectionEditor::card(const RowSnapshot &row)
{
    m_collection.columns = row.collection->columns;
    build();
    return m_card;
}

void CollectionEditor::build()
{
    m_actionButtons.clear();
    StackPanel content;
    content.Padding({16, 12, 16, 12});
    content.Spacing(8);

    // The toolbar: Add, Edit, Import, the descriptor's named actions, Delete.
    StackPanel toolbar;
    toolbar.Orientation(Orientation::Horizontal);
    toolbar.Spacing(8);
    m_addButton = nullptr;
    if (!m_collection.addLabel.isEmpty()) {
        m_addButton = Button();
        m_addButton.Content(box_value(hs(m_collection.addLabel)));
        m_addButton.Click([weak = weak_from_this()](const auto &, const auto &) {
            if (auto self = weak.lock()) {
                self->openRecordDialog(-1);
            }
        });
        toolbar.Children().Append(m_addButton);
    }
    m_editButton = nullptr;
    if (!m_collection.editLabel.isEmpty()) {
        m_editButton = Button();
        m_editButton.Content(box_value(hs(m_collection.editLabel)));
        m_editButton.Click([weak = weak_from_this()](const auto &, const auto &) {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            const QList<int> selected = self->selectedIndexes();
            if (selected.size() == 1) {
                self->openRecordDialog(selected.first());
            }
        });
        toolbar.Children().Append(m_editButton);
    }
    if (!m_collection.importLabel.isEmpty()) {
        Button import;
        import.Content(box_value(hs(m_collection.importLabel)));
        import.Click([weak = weak_from_this()](const auto &, const auto &) {
            if (auto self = weak.lock()) {
                self->importFromFile();
            }
        });
        toolbar.Children().Append(import);
    }
    for (const RowOption &action : m_collection.actions) {
        Button button;
        button.Content(box_value(hs(action.label)));
        button.Click([weak = weak_from_this(), id = action.id](const auto &, const auto &) {
            if (auto self = weak.lock()) {
                self->runAction(id);
            }
        });
        m_actionButtons.append({action.id, button});
        toolbar.Children().Append(button);
    }
    m_deleteButton = Button();
    m_deleteButton.Content(box_value(hs(m_collection.deleteLabel)));
    m_deleteButton.Click([weak = weak_from_this()](const auto &, const auto &) {
        if (auto self = weak.lock()) {
            self->removeSelected();
        }
    });
    toolbar.Children().Append(m_deleteButton);
    content.Children().Append(toolbar);

    // The header row, aligned with the cells by sharing their column table.
    const QList<CollectionColumnSnapshot> columns = tableColumns(m_collection);
    m_header = columnGrid(columns);
    m_header.Padding({12, 0, 12, 0});
    for (qsizetype index = 0; index < columns.size(); ++index) {
        TextBlock title = cellText(columns.at(index).title,
                                   L"SettingsCardDescriptionStyle",
                                   &m_host);
        Grid::SetColumn(title, static_cast<int32_t>(index));
        m_header.Children().Append(title);
    }
    content.Children().Append(m_header);

    m_list = ListView();
    m_list.SelectionMode(ListViewSelectionMode::Extended);
    // As tall as its records, up to the descriptor's height, past which the
    // list scrolls.
    m_list.MaxHeight(m_collection.minimumHeight);
    // The cells lay themselves out; the container must hand them the row's
    // full width rather than centre-left them.
    Style container(xaml_typename<ListViewItem>());
    container.Setters().Append(Setter(Control::HorizontalContentAlignmentProperty(),
                                      box_value(HorizontalAlignment::Stretch)));
    container.Setters().Append(Setter(Control::PaddingProperty(),
                                      box_value(Thickness{12, 4, 12, 4})));
    container.Setters().Append(Setter(FrameworkElement::MinHeightProperty(), box_value(36.0)));
    m_list.ItemContainerStyle(container);
    m_list.SelectionChanged([weak = weak_from_this()](const auto &, const auto &) {
        if (auto self = weak.lock()) {
            self->updateToolbar();
        }
    });
    // Double-clicking a row opens it, as the edit command does; a double-click
    // on a cell's own control stays that control's.
    if (!m_collection.editLabel.isEmpty()) {
        m_list.DoubleTapped([weak = weak_from_this()](const IInspectable &,
                                                      const Input::DoubleTappedRoutedEventArgs &args) {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            const int index = self->recordAt(args.OriginalSource());
            if (index >= 0 && !self->m_records.at(index).locked) {
                self->openRecordDialog(index);
            }
        });
    }
    content.Children().Append(m_list);
    // The empty state stands in for the header and the list: what goes here,
    // with the accented Add above it as the way to start.
    m_empty = nullptr;
    if (!m_collection.emptyTitle.isEmpty()) {
        m_empty = StackPanel();
        m_empty.Spacing(2);
        m_empty.Padding({0, 4, 0, 4});
        m_empty.Children().Append(styledTextBlock(m_collection.emptyTitle, L"BodyStrongTextBlockStyle"));
        m_empty.Children().Append(
            secondaryTextBlock(m_collection.emptyHelp, L"SettingsCardDescriptionStyle", m_host));
        content.Children().Append(m_empty);
    }

    m_problems = InfoBar();
    m_problems.Severity(InfoBarSeverity::Error);
    m_problems.IsClosable(false);
    content.Children().Append(m_problems);
    showProblems(m_lastProblems, m_lastProblemsTitle);

    m_card = cardContainer(content);
    rebuildRows();
}

void CollectionEditor::rebuildRows()
{
    m_list.Items().Clear();
    QList<QVariantMap> values;
    for (const Record &record : m_records) {
        values.append(record.values);
    }
    const QStringList badges = m_host.model->badgesFor(values, m_rowId);
    const QStringList detailBadges = m_host.model->detailBadgesFor(values, m_rowId);
    const QList<CollectionColumnSnapshot> columns = tableColumns(m_collection);
    for (qsizetype index = 0; index < m_records.size(); ++index) {
        Grid row = columnGrid(columns);
        row.Tag(box_value(static_cast<int32_t>(index)));
        for (qsizetype columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
            const CollectionColumnSnapshot &column = columns.at(columnIndex);
            const UIElement cell = cellFor(column,
                                           static_cast<int>(index),
                                           column.stretch ? badges.value(index) : QString(),
                                           column.stretch ? detailBadges.value(index) : QString());
            Grid::SetColumn(cell.as<FrameworkElement>(), static_cast<int32_t>(columnIndex));
            row.Children().Append(cell);
        }
        m_list.Items().Append(row);
    }
    updateToolbar();
}

UIElement CollectionEditor::cellFor(const CollectionColumnSnapshot &column,
                                   int recordIndex,
                                   const QString &badgeText,
                                   const QString &detailBadgeText)
{
    const Record &record = m_records.at(recordIndex);
    const QVariant value = record.values.value(column.id);
    const QString detail = column.detailColumn.isEmpty()
        ? QString()
        : record.values.value(column.detailColumn).toString();
    QString tooltip = detail.isEmpty()
        ? m_host.model->tooltipForColumn(column.id, m_rowId, record.values)
        : detail;
    UIElement cell{nullptr};
    if (column.kind == ColumnKind::ChoiceSet) {
        // The labels can outrun the column, so the tooltip says them whole.
        tooltip = m_host.model->choiceSetText(m_rowId, column.id, value.toStringList());
        cell = cellText(tooltip, L"SettingsCardBodyStyle", nullptr);
    } else if (record.locked || column.kind == ColumnKind::ReadOnly
               // A cell with a line under it only shows its text; the record
               // dialog edits it.
               || !column.detailColumn.isEmpty()) {
        cell = cellText(displayText(column, value),
                        column.kind == ColumnKind::ReadOnly ? L"SettingsCardDescriptionStyle"
                                                            : L"SettingsCardBodyStyle",
                        column.kind == ColumnKind::ReadOnly ? &m_host : nullptr);
    } else if (column.kind == ColumnKind::Toggle) {
        CheckBox box;
        box.MinWidth(0);
        box.IsChecked(value.toBool());
        box.Click([weak = weak_from_this(), columnId = column.id, recordIndex](
                      const IInspectable &sender, const auto &) {
            if (auto self = weak.lock()) {
                self->m_records[recordIndex].values[columnId] =
                    sender.as<CheckBox>().IsChecked().GetBoolean();
                self->save();
            }
        });
        cell = box;
    } else if (column.kind == ColumnKind::Choice) {
        ComboBox combo;
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        int selected = -1;
        for (const RowOption &option : column.options) {
            ComboBoxItem item;
            item.Content(box_value(hs(option.label)));
            item.Tag(box_value(hs(option.id)));
            item.IsEnabled(option.enabled);
            if (option.id == value.toString()) {
                selected = combo.Items().Size();
            }
            combo.Items().Append(item);
        }
        combo.SelectedIndex(selected);
        combo.SelectionChanged([weak = weak_from_this(), columnId = column.id, recordIndex](
                                   const IInspectable &sender, const auto &) {
            const auto item = sender.as<ComboBox>().SelectedItem();
            if (!item) {
                return;
            }
            if (auto self = weak.lock()) {
                self->m_records[recordIndex].values[columnId] =
                    qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag()));
                self->save();
            }
        });
        cell = combo;
    } else {
        // Text in a record, saved when the field is done rather than on every
        // keystroke: the collections normalise on save, and a term that
        // momentarily duplicates another one has to survive being typed.
        TextBox box;
        box.Text(hs(value.toString()));
        if (column.multiline) {
            makeMultiline(box);
        }
        const auto commit = [weak = weak_from_this(), columnId = column.id, recordIndex](
                                const TextBox &box) {
            auto self = weak.lock();
            if (!self) {
                return;
            }
            const QString text = qs(box.Text());
            if (self->m_records[recordIndex].values.value(columnId).toString() != text) {
                self->m_records[recordIndex].values[columnId] = text;
                self->save();
            }
        };
        box.LostFocus([commit](const IInspectable &sender, const auto &) {
            commit(sender.as<TextBox>());
        });
        box.KeyDown([commit, multiline = column.multiline](const IInspectable &sender,
                                                           const Input::KeyRoutedEventArgs &args) {
            if (!multiline && args.Key() == Windows::System::VirtualKey::Enter) {
                commit(sender.as<TextBox>());
            }
        });
        cell = box;
    }
    if (!badgeText.isEmpty()) {
        // After the cell, as Home's Writing Profiles sit after a name.
        cell = withBadge(cell, namedBadge(badgeText, L"RatingBadgeAccent", m_host), false);
    }
    const QString detailLine = detail.simplified();
    if (!detailLine.isEmpty() || !detailBadgeText.isEmpty()) {
        // The detail's one line under the cell's own, muted and cut at the
        // end, starting with its own pill in the neutral tone where there is
        // one; a pill with no detail still gets the line.
        UIElement second = cellText(detailLine, L"CaptionTextBlockStyle", &m_host);
        if (!detailBadgeText.isEmpty()) {
            second = withBadge(second, namedBadge(detailBadgeText, L"RatingBadgeNeutral", m_host), true);
        }
        StackPanel lines;
        lines.VerticalAlignment(VerticalAlignment::Center);
        lines.Children().Append(cell);
        lines.Children().Append(second);
        cell = lines;
    }
    if (!tooltip.isEmpty()) {
        ToolTipService::SetToolTip(cell, box_value(hs(tooltip)));
    }
    return cell;
}

QList<int> CollectionEditor::selectedIndexes() const
{
    QList<int> indexes;
    for (const auto &item : m_list.SelectedItems()) {
        indexes.append(unbox_value<int32_t>(item.as<Grid>().Tag()));
    }
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

int CollectionEditor::recordAt(const IInspectable &element) const
{
    for (DependencyObject node = element.try_as<DependencyObject>(); node;
         node = Media::VisualTreeHelper::GetParent(node)) {
        if (const auto item = node.try_as<ListViewItem>()) {
            const IInspectable row = m_list.ItemFromContainer(item);
            return row ? unbox_value<int32_t>(row.as<Grid>().Tag()) : -1;
        }
        if (node.try_as<Control>()) {
            return -1;
        }
    }
    return -1;
}

void CollectionEditor::updateToolbar()
{
    if (m_empty) {
        const bool empty = m_records.isEmpty();
        m_empty.Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        m_header.Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
        m_list.Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
        if (m_addButton) {
            m_addButton.Style(empty ? Application::Current()
                                          .Resources()
                                          .Lookup(box_value(L"AccentButtonStyle"))
                                          .as<Style>()
                                    : Style{nullptr});
        }
    }
    const QList<int> selected = selectedIndexes();
    bool removable = false;
    for (int index : selected) {
        removable = removable || !m_records.at(index).locked;
    }
    m_deleteButton.IsEnabled(removable);
    if (m_editButton) {
        m_editButton.IsEnabled(selected.size() == 1 && !m_records.at(selected.first()).locked);
    }
    for (const auto &[actionId, button] : m_actionButtons) {
        if (actionId == kUndoDelete) {
            button.IsEnabled(!m_deleted.isEmpty());
        } else if (actionId == kUndoLatestLearn) {
            bool anyEditable = false;
            for (const Record &record : m_records) {
                anyEditable = anyEditable || !record.locked;
            }
            button.IsEnabled(anyEditable);
        }
    }
}

QList<QVariantMap> CollectionEditor::editableRecords() const
{
    return editableRecords(m_records);
}

QList<QVariantMap> CollectionEditor::editableRecords(const QList<Record> &all)
{
    QList<QVariantMap> records;
    for (const Record &record : all) {
        if (!record.locked) {
            records.append(record.values);
        }
    }
    return records;
}

void CollectionEditor::save()
{
    const auto records = editableRecords();
    const auto problems = m_host.model->save(records, m_rowId, m_savedRecords);
    if (problems.isEmpty()) m_savedRecords = records;
    showProblems(problems);
    // Rows elsewhere derive from these records — the vocabulary limit — so the
    // pane re-derives; this editor survives it by being cached on the host.
    if (m_host.refresh) {
        m_host.refresh();
    }
}

void CollectionEditor::showProblems(const QStringList &problems, const QString &title)
{
    m_lastProblems = problems;
    m_lastProblemsTitle = title;
    m_problems.Title(hs(title));
    m_problems.Message(hs(problems.join(QLatin1Char('\n'))));
    m_problems.IsOpen(!problems.isEmpty());
}

void CollectionEditor::openRecordDialog(int recordIndex)
{
    const bool adding = recordIndex < 0;
    // Edits start from the record as it is, so the keys no field shows survive.
    const QVariantMap original = adding ? m_collection.blankRecord : m_records.at(recordIndex).values;
    ContentDialog dialog;
    dialog.XamlRoot(m_host.xamlRoot());
    // The dialog opens in the popup layer, outside the window's RequestedTheme.
    if (m_host.effectiveTheme) {
        dialog.RequestedTheme(m_host.effectiveTheme());
    }
    dialog.Title(box_value(hs(adding ? m_collection.addDialogTitle : recordName(m_collection, original))));
    dialog.PrimaryButtonText(adding ? L"Add" : L"OK");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);

    StackPanel fields;
    fields.Spacing(12);
    fields.MinWidth(360);
    // What must hold before the primary button takes the record, rechecked as
    // the fields change. Emptied when the dialog closes, which ends the cycle
    // through the fields' handlers.
    QList<std::function<bool()>> checks;
    const auto recheck = std::make_shared<std::function<void()>>();
    // One field per column a person may fill, so the record is checked before
    // it is kept.
    QList<QPair<QString, std::function<QVariant()>>> readers;
    // The checkboxes so far, by column, for a later one enabled by them.
    QList<QPair<QString, CheckBox>> toggles;
    bool named = false;
    for (const CollectionColumnSnapshot &column : m_collection.columns) {
        if (column.kind == ColumnKind::ReadOnly) {
            continue;
        }
        const QVariant value = original.value(column.id);
        UIElement field{nullptr};
        if (column.kind == ColumnKind::Toggle) {
            CheckBox box;
            box.Content(box_value(hs(column.title)));
            box.IsChecked(value.toBool());
            const auto enabler = std::find_if(toggles.cbegin(), toggles.cend(), [&](const auto &toggle) {
                return toggle.first == column.enabledBy;
            });
            if (enabler != toggles.cend()) {
                const CheckBox &other = enabler->second;
                box.IsEnabled(other.IsChecked().GetBoolean());
                const auto follow = [box](const IInspectable &sender, const RoutedEventArgs &) {
                    box.IsEnabled(sender.as<CheckBox>().IsChecked().GetBoolean());
                };
                other.Checked(follow);
                other.Unchecked(follow);
            }
            toggles.append({column.id, box});
            readers.append({column.id, [box] {
                                return QVariant(box.IsChecked().GetBoolean());
                            }});
            field = box;
        } else if (column.kind == ColumnKind::Choice) {
            ComboBox combo;
            combo.Header(box_value(hs(column.title)));
            combo.HorizontalAlignment(HorizontalAlignment::Stretch);
            int selected = -1;
            for (const RowOption &option : column.options) {
                ComboBoxItem item;
                item.Content(box_value(hs(option.label)));
                item.Tag(box_value(hs(option.id)));
                if (option.id == value.toString()) {
                    selected = combo.Items().Size();
                }
                combo.Items().Append(item);
            }
            combo.SelectedIndex(selected);
            readers.append({column.id, [combo]() -> QVariant {
                                const auto item = combo.SelectedItem();
                                return item ? qs(unbox_value<hstring>(
                                                item.as<ComboBoxItem>().Tag()))
                                            : QString();
                            }});
            field = combo;
        } else if (column.kind == ColumnKind::ChoiceSet) {
            // Every option, or only the ticked ones: the boxes sit under the
            // second choice and count only while it is chosen.
            RadioButtons scope;
            scope.Header(box_value(hs(column.title)));
            scope.Items().Append(box_value(hs(column.everyChoice)));
            scope.Items().Append(box_value(hs(column.someChoice)));
            StackPanel boxes;
            // In line with the second choice's label, past its circle.
            boxes.Margin({28, 0, 0, 0});
            QList<QPair<QString, CheckBox>> options;
            const QStringList stored = value.toStringList();
            bool limited = false;
            for (const RowOption &option : column.options) {
                const bool ticked = stored.contains(option.id);
                limited = limited || ticked;
                CheckBox box;
                box.Content(box_value(hs(option.label)));
                box.IsChecked(ticked);
                box.Click([recheck](const auto &, const auto &) {
                    if (*recheck) {
                        (*recheck)();
                    }
                });
                boxes.Children().Append(box);
                options.append({option.id, box});
            }
            const auto enableBoxes = [options](bool enabled) {
                for (const auto &[id, box] : options) {
                    box.IsEnabled(enabled);
                }
            };
            scope.SelectedIndex(limited ? 1 : 0);
            enableBoxes(limited);
            scope.SelectionChanged([enableBoxes, recheck](const IInspectable &sender, const auto &) {
                enableBoxes(sender.as<RadioButtons>().SelectedIndex() == 1);
                if (*recheck) {
                    (*recheck)();
                }
            });
            // The ticked ids while the second choice holds, otherwise none,
            // which means every option.
            const auto chosen = [scope, options] {
                QStringList ids;
                if (scope.SelectedIndex() != 1) {
                    return ids;
                }
                for (const auto &[id, box] : options) {
                    if (box.IsChecked().GetBoolean()) {
                        ids.append(id);
                    }
                }
                return ids;
            };
            readers.append({column.id, [chosen] { return QVariant(chosen()); }});
            checks.append([scope, chosen] { return scope.SelectedIndex() != 1 || !chosen().isEmpty(); });
            StackPanel choice;
            choice.Children().Append(scope);
            choice.Children().Append(boxes);
            field = choice;
        } else {
            TextBox box;
            box.Header(box_value(hs(column.title)));
            box.Text(hs(value.toString()));
            box.PlaceholderText(hs(column.placeholder));
            if (column.multiline) {
                makeMultiline(box);
            }
            readers.append({column.id, [box] { return QVariant(qs(box.Text())); }});
            // The first text field names the record, as on Linux, so there is
            // nothing to keep until it holds something.
            if (!named) {
                named = true;
                checks.append([box] { return !qs(box.Text()).trimmed().isEmpty(); });
                box.TextChanged([recheck](const auto &, const auto &) {
                    if (*recheck) {
                        (*recheck)();
                    }
                });
            }
            field = box;
        }
        if (column.help.isEmpty()) {
            fields.Children().Append(field);
            continue;
        }
        // Under the field, muted and small, as a card row's description.
        StackPanel withHelp;
        withHelp.Spacing(4);
        withHelp.Children().Append(field);
        withHelp.Children().Append(
            secondaryTextBlock(column.help, L"SettingsCardDescriptionStyle", m_host));
        fields.Children().Append(withHelp);
    }
    TextBlock refusals;
    refusals.Style(Application::Current()
                       .Resources()
                       .Lookup(box_value(L"SettingsCardDescriptionStyle"))
                       .as<Style>());
    refusals.Visibility(Visibility::Collapsed);
    fields.Children().Append(refusals);
    // A record of many fields outgrows a small window.
    ScrollViewer scroller;
    scroller.Content(fields);
    dialog.Content(scroller);

    *recheck = [dialog, checks] {
        dialog.IsPrimaryButtonEnabled(
            std::all_of(checks.cbegin(), checks.cend(), [](const auto &check) { return check(); }));
    };
    (*recheck)();
    dialog.Closed([recheck](const ContentDialog &, const ContentDialogClosedEventArgs &) {
        *recheck = nullptr;
    });

    dialog.PrimaryButtonClick([weak = weak_from_this(), readers, refusals, recordIndex, original](
                                  const ContentDialog &,
                                  const ContentDialogButtonClickEventArgs &args) {
        auto self = weak.lock();
        if (!self) {
            return;
        }
        QVariantMap record = original;
        for (const auto &[columnId, read] : readers) {
            record.insert(columnId, read());
        }
        QList<Record> proposed = self->m_records;
        if (recordIndex < 0) {
            proposed.append({record, false});
        } else {
            proposed[recordIndex].values = record;
        }
        const QStringList problems =
            self->m_host.model->problemsWith(CollectionEditor::editableRecords(proposed), self->m_rowId);
        if (!problems.isEmpty()) {
            // Refused: the dialog stays open with the record still in it.
            refusals.Text(hs(problems.join(QLatin1Char('\n'))));
            refusals.Visibility(Visibility::Visible);
            args.Cancel(true);
            return;
        }
        self->m_records = proposed;
        self->rebuildRows();
        self->save();
    });
    dialog.ShowAsync();
}

winrt::fire_and_forget CollectionEditor::importFromFile()
{
    auto weak = weak_from_this();
    try {
        Windows::Storage::Pickers::FileOpenPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_host.hwnd()));
        if (m_collection.importFileExtensions.isEmpty()) {
            picker.FileTypeFilter().Append(L"*");
        } else {
            for (const QString &extension : m_collection.importFileExtensions) {
                picker.FileTypeFilter().Append(hs(QStringLiteral(".") + extension));
            }
        }
        const Windows::Storage::StorageFile file = co_await picker.PickSingleFileAsync();
        auto self = weak.lock();
        if (!self || !file) {
            co_return;
        }
        QFile source(qs(file.Path()));
        if (!source.open(QIODevice::ReadOnly)) {
            self->showProblems({QStringLiteral("Could not read %1.").arg(qs(file.Name()))},
                               self->m_collection.importFailureTitle);
            co_return;
        }
        const SettingsModel::ImportResult result =
            self->m_host.model->recordsImportedFrom(source.readAll(),
                                                    self->editableRecords(),
                                                    self->m_rowId);
        if (!result.records) {
            self->showProblems({result.problem}, self->m_collection.importFailureTitle);
            co_return;
        }
        QList<Record> merged;
        for (const Record &record : self->m_records) {
            if (record.locked) {
                merged.append(record);
            }
        }
        for (const QVariantMap &values : *result.records) {
            merged.append({values, false});
        }
        self->m_records = merged;
        self->rebuildRows();
        self->save();
    } catch (const winrt::hresult_error &error) {
        if (auto self = weak.lock()) {
            self->showProblems({qs(error.message())}, self->m_collection.importFailureTitle);
        }
    }
}

void CollectionEditor::removeSelected()
{
    const QList<int> doomed = selectedIndexes();
    bool removed = false;
    for (qsizetype position = doomed.size() - 1; position >= 0; --position) {
        const int index = doomed.at(position);
        if (m_records.at(index).locked) {
            continue;
        }
        m_deleted.append({index, m_records.takeAt(index)});
        removed = true;
    }
    if (!removed) {
        return;
    }
    rebuildRows();
    save();
}

void CollectionEditor::runAction(const QString &actionId)
{
    if (actionId == kUndoDelete) {
        if (m_deleted.isEmpty()) {
            return;
        }
        const auto [index, record] = m_deleted.takeLast();
        m_records.insert(std::min(index, m_records.size()), record);
    } else if (actionId == kUndoLatestLearn) {
        for (qsizetype index = 0; index < m_records.size(); ++index) {
            if (!m_records.at(index).locked) {
                m_deleted.append({index, m_records.takeAt(index)});
                break;
            }
        }
    }
    rebuildRows();
    save();
}

} // namespace speecher::win
