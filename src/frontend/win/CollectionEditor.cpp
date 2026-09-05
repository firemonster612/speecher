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
#include <winrt/Microsoft.UI.Xaml.Input.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

const QString kUndoDelete = QStringLiteral("undoDelete");
const QString kUndoLatestLearn = QStringLiteral("undoLatestLearn");

// How much of the list's width a column takes. The descriptor names the one
// that takes the leftover; a flag needs no more than its checkbox, and the
// rest get widths their values fit in.
GridLength columnWidth(const CollectionColumnSnapshot &column)
{
    if (column.stretch) {
        return {1, GridUnitType::Star};
    }
    switch (column.kind) {
    case ColumnKind::Toggle:
        return {56, GridUnitType::Pixel};
    case ColumnKind::Choice:
        return {150, GridUnitType::Pixel};
    case ColumnKind::ReadOnly:
        return {110, GridUnitType::Pixel};
    case ColumnKind::Text:
        return {140, GridUnitType::Pixel};
    }
    return {0, GridUnitType::Auto};
}

Grid columnGrid(const QList<CollectionColumnSnapshot> &columns)
{
    Grid grid;
    grid.ColumnSpacing(8);
    for (const CollectionColumnSnapshot &column : columns) {
        ColumnDefinition definition;
        definition.Width(columnWidth(column));
        grid.ColumnDefinitions().Append(definition);
    }
    return grid;
}

TextBlock cellText(const QString &text, const wchar_t *styleKey, bool secondary)
{
    TextBlock block = styledTextBlock(text, styleKey, secondary);
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

} // namespace

CollectionEditor::CollectionEditor(const RowSnapshot &row, PaneHost &host)
    : m_rowId(row.id)
    , m_rowLabel(row.label)
    , m_collection(*row.collection)
    , m_host(host)
{
    // The settings' records, taken once: from here on the editor's copy is the
    // live one, so a rebuild cannot undo an edit.
    const QList<QVariantMap> stored = row.value.value<QList<QVariantMap>>();
    for (qsizetype index = 0; index < stored.size(); ++index) {
        m_records.append({stored.at(index), index < m_collection.lockedRecordCount});
    }
}

UIElement CollectionEditor::card()
{
    if (!m_card) {
        build();
    }
    detachFromParent(m_card);
    return m_card;
}

void CollectionEditor::build()
{
    StackPanel content;
    content.Padding({16, 12, 16, 12});
    content.Spacing(8);

    // The toolbar: Add, Import, the descriptor's named actions, Delete.
    StackPanel toolbar;
    toolbar.Orientation(Orientation::Horizontal);
    toolbar.Spacing(8);
    if (!m_collection.addLabel.isEmpty()) {
        Button add;
        add.Content(box_value(hs(m_collection.addLabel)));
        add.Click([weak = weak_from_this()](const auto &, const auto &) {
            if (auto self = weak.lock()) {
                self->openAddDialog();
            }
        });
        toolbar.Children().Append(add);
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
    m_deleteButton.Content(box_value(L"Delete selected"));
    m_deleteButton.Click([weak = weak_from_this()](const auto &, const auto &) {
        if (auto self = weak.lock()) {
            self->removeSelected();
        }
    });
    toolbar.Children().Append(m_deleteButton);
    content.Children().Append(toolbar);

    // The header row, aligned with the cells by sharing their column table.
    Grid header = columnGrid(m_collection.columns);
    header.Padding({12, 0, 12, 0});
    for (qsizetype index = 0; index < m_collection.columns.size(); ++index) {
        TextBlock title = cellText(m_collection.columns.at(index).title,
                                   L"SettingsCardDescriptionStyle",
                                   true);
        Grid::SetColumn(title, static_cast<int32_t>(index));
        header.Children().Append(title);
    }
    content.Children().Append(header);

    m_list = ListView();
    m_list.SelectionMode(ListViewSelectionMode::Extended);
    m_list.Height(m_collection.minimumHeight);
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
    content.Children().Append(m_list);

    m_problems = InfoBar();
    m_problems.Severity(InfoBarSeverity::Error);
    m_problems.IsClosable(false);
    m_problems.IsOpen(false);
    content.Children().Append(m_problems);

    m_card = cardContainer(content);
    rebuildRows();
}

void CollectionEditor::rebuildRows()
{
    m_list.Items().Clear();
    for (qsizetype index = 0; index < m_records.size(); ++index) {
        Grid row = columnGrid(m_collection.columns);
        row.Tag(box_value(static_cast<int32_t>(index)));
        for (qsizetype columnIndex = 0; columnIndex < m_collection.columns.size(); ++columnIndex) {
            const UIElement cell = cellFor(m_collection.columns.at(columnIndex),
                                           static_cast<int>(index));
            Grid::SetColumn(cell.as<FrameworkElement>(), static_cast<int32_t>(columnIndex));
            row.Children().Append(cell);
        }
        m_list.Items().Append(row);
    }
    updateToolbar();
}

UIElement CollectionEditor::cellFor(const CollectionColumnSnapshot &column, int recordIndex)
{
    const Record &record = m_records.at(recordIndex);
    const QVariant value = record.values.value(column.id);
    UIElement cell{nullptr};
    if (record.locked || column.kind == ColumnKind::ReadOnly) {
        cell = cellText(displayText(column, value),
                        column.kind == ColumnKind::ReadOnly ? L"SettingsCardDescriptionStyle"
                                                            : L"SettingsCardBodyStyle",
                        column.kind == ColumnKind::ReadOnly);
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
        box.KeyDown([commit](const IInspectable &sender, const Input::KeyRoutedEventArgs &args) {
            if (args.Key() == Windows::System::VirtualKey::Enter) {
                commit(sender.as<TextBox>());
            }
        });
        cell = box;
    }
    const QString tooltip = m_host.model->tooltipForColumn(column.id, m_rowId, record.values);
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

void CollectionEditor::updateToolbar()
{
    bool removable = false;
    for (int index : selectedIndexes()) {
        removable = removable || !m_records.at(index).locked;
    }
    m_deleteButton.IsEnabled(removable);
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
    QList<QVariantMap> records;
    for (const Record &record : m_records) {
        if (!record.locked) {
            records.append(record.values);
        }
    }
    return records;
}

void CollectionEditor::save()
{
    showProblems(m_host.model->save(editableRecords(), m_rowId));
    // Rows elsewhere derive from these records — the vocabulary limit — so the
    // pane re-derives; this editor survives it by being cached on the host.
    if (m_host.refresh) {
        m_host.refresh();
    }
}

void CollectionEditor::showProblems(const QStringList &problems)
{
    m_problems.Message(hs(problems.join(QLatin1Char('\n'))));
    m_problems.IsOpen(!problems.isEmpty());
}

void CollectionEditor::openAddDialog()
{
    ContentDialog dialog;
    dialog.XamlRoot(m_host.xamlRoot());
    dialog.Title(box_value(hs(m_collection.addLabel)));
    dialog.PrimaryButtonText(L"Add");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);

    StackPanel fields;
    fields.Spacing(12);
    fields.MinWidth(360);
    // One field per column a person may fill, over the descriptor's blank
    // record, so the record is checked before it exists.
    QList<QPair<QString, std::function<QVariant()>>> readers;
    for (const CollectionColumnSnapshot &column : m_collection.columns) {
        if (column.kind == ColumnKind::ReadOnly) {
            continue;
        }
        const QVariant blank = m_collection.blankRecord.value(column.id);
        if (column.kind == ColumnKind::Toggle) {
            CheckBox box;
            box.Content(box_value(hs(column.title)));
            box.IsChecked(blank.toBool());
            readers.append({column.id, [box] {
                                return QVariant(box.IsChecked().GetBoolean());
                            }});
            fields.Children().Append(box);
        } else if (column.kind == ColumnKind::Choice) {
            ComboBox combo;
            combo.Header(box_value(hs(column.title)));
            combo.HorizontalAlignment(HorizontalAlignment::Stretch);
            int selected = -1;
            for (const RowOption &option : column.options) {
                ComboBoxItem item;
                item.Content(box_value(hs(option.label)));
                item.Tag(box_value(hs(option.id)));
                if (option.id == blank.toString()) {
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
            fields.Children().Append(combo);
        } else {
            TextBox box;
            box.Header(box_value(hs(column.title)));
            box.Text(hs(blank.toString()));
            // Snippets hold several lines; a single-line field would fold them.
            if (m_rowId == QStringLiteral("bindingRules")
                && column.id == QStringLiteral("replacement")) {
                box.AcceptsReturn(true);
                box.Height(96);
                box.TextWrapping(TextWrapping::Wrap);
            }
            readers.append({column.id, [box] { return QVariant(qs(box.Text())); }});
            fields.Children().Append(box);
        }
    }
    TextBlock refusals;
    refusals.Style(Application::Current()
                       .Resources()
                       .Lookup(box_value(L"SettingsCardDescriptionStyle"))
                       .as<Style>());
    refusals.Visibility(Visibility::Collapsed);
    fields.Children().Append(refusals);
    dialog.Content(fields);

    dialog.PrimaryButtonClick([weak = weak_from_this(), readers, refusals](
                                  const ContentDialog &,
                                  const ContentDialogButtonClickEventArgs &args) {
        auto self = weak.lock();
        if (!self) {
            return;
        }
        QVariantMap draft = self->m_collection.blankRecord;
        for (const auto &[columnId, read] : readers) {
            draft.insert(columnId, read());
        }
        const QStringList problems =
            self->m_host.model->problemsWith(self->editableRecords() + QList<QVariantMap>{draft},
                                             self->m_rowId);
        if (!problems.isEmpty()) {
            // Refused: the dialog stays open with the record still in it.
            refusals.Text(hs(problems.join(QLatin1Char('\n'))));
            refusals.Visibility(Visibility::Visible);
            args.Cancel(true);
            return;
        }
        self->m_records.append({draft, false});
        self->rebuildRows();
        self->save();
    });
    dialog.ShowAsync();
}

winrt::fire_and_forget CollectionEditor::importFromFile()
{
    auto weak = weak_from_this();
    Windows::Storage::Pickers::FileOpenPicker picker;
    picker.as<::IInitializeWithWindow>()->Initialize(m_host.hwnd());
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
        self->showProblems({QStringLiteral("Could not read %1.").arg(qs(file.Name()))});
        co_return;
    }
    const SettingsModel::ImportResult result =
        self->m_host.model->recordsImportedFrom(source.readAll(),
                                                self->editableRecords(),
                                                self->m_rowId);
    if (!result.records) {
        self->showProblems({result.problem});
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
        m_deleted.append(m_records.takeAt(index));
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
        qsizetype insertAt = m_records.size();
        for (qsizetype index = 0; index < m_records.size(); ++index) {
            if (!m_records.at(index).locked) {
                insertAt = index;
                break;
            }
        }
        m_records.insert(insertAt, m_deleted.takeLast());
    } else if (actionId == kUndoLatestLearn) {
        for (qsizetype index = 0; index < m_records.size(); ++index) {
            if (!m_records.at(index).locked) {
                m_deleted.append(m_records.takeAt(index));
                break;
            }
        }
    }
    rebuildRows();
    save();
}

} // namespace speecher::win
