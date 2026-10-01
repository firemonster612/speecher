#include "frontend/qt/CollectionRow.h"

#include "ui/InlineMessage.h"
#include "ui/InsightsCharts.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace speecher {

namespace {

// Edits a cell in a QPlainTextEdit, where Return starts a new line rather
// than committing the edit.
class MultilineDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return new QPlainTextEdit(parent);
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        static_cast<QPlainTextEdit *>(editor)->setPlainText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        model->setData(index, static_cast<QPlainTextEdit *>(editor)->toPlainText(), Qt::EditRole);
    }
};

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QString optionLabel(const CollectionColumn &column, const QString &id, const AppSettings &settings)
{
    if (!column.options) {
        return id;
    }
    for (const RowOption &option : column.options(settings)) {
        if (option.id == id) {
            return option.label;
        }
    }
    return id;
}

// What a record's dialog is titled after: its first text column, which names it.
QString recordName(const CollectionDescriptor &collection, const QVariantMap &record)
{
    for (const CollectionColumn &column : collection.columns) {
        if (column.kind == ColumnKind::Text) {
            return record.value(column.id).toString();
        }
    }
    return {};
}

// Buttons are named after the collection they act on, so a test can find them
// without the descriptor spelling out a widget name.
QString buttonObjectName(const QString &verb, const QString &collectionId)
{
    return verb + collectionId.at(0).toUpper() + collectionId.mid(1);
}

class CollectionEditor final : public QWidget {
public:
    CollectionEditor(const SettingsRow &descriptor,
                     QWidget *parent,
                     std::function<void()> notifyChanged);

    QList<QVariantMap> records() const;
    // What the settings hold, which starts the editor's history over.
    void setRecords(const QList<QVariantMap> &records);
    // Keeps the settings a choice column's options come from, and re-derives
    // the badges beside each record for them.
    void refresh(const AppSettings &settings);
    // A collection whose gate is closed keeps its records readable and stops
    // taking edits.
    void setEditable(bool editable);

private:
    void showRecords(const QList<QVariantMap> &records);
    void appendRecord(const QVariantMap &record, bool locked);
    QList<QVariantMap> lockedRecords() const;
    void importRecords();
    // A negative row adds a record.
    void editRecord(int row);
    void runAction(const QString &actionId);
    QList<int> selectedEditableRows() const;
    void updateButtons();

    CollectionDescriptor m_collection;
    AppSettings m_settings;
    QTableWidget *m_table;
    QPushButton *m_add = nullptr;
    QPushButton *m_delete;
    QLabel *m_empty = nullptr;
    QHash<QString, QPushButton *> m_actions;
    int m_lockedCount;
    bool m_editable = true;
    QPushButton *m_import = nullptr;
    // What Delete took and its index among the editable records, newest last,
    // so undo can put it back in its place.
    QList<QPair<qsizetype, QVariantMap>> m_deleted;
    std::function<void()> m_notifyChanged;
};

// The whole record a row stands for, including the keys no column shows. The
// hidden vertical header is the one per-row place every column kind leaves
// alone, cell widgets included.
QVariantMap rowRecord(const QTableWidget *table, int row)
{
    const QTableWidgetItem *carrier = table->verticalHeaderItem(row);
    return carrier ? carrier->data(Qt::UserRole).toMap() : QVariantMap();
}

CollectionEditor::CollectionEditor(const SettingsRow &descriptor,
                                   QWidget *parent,
                                   std::function<void()> notifyChanged)
    : QWidget(parent)
    , m_collection(descriptor.collection)
    , m_table(new QTableWidget(this))
    , m_delete(new QPushButton(m_collection.deleteLabel, this))
    , m_lockedCount(m_collection.lockedRecordCount ? m_collection.lockedRecordCount() : 0)
    , m_notifyChanged(std::move(notifyChanged))
{
    auto *layout = new QVBoxLayout(this);
    // Same inset as a card row so the block's text lines up with row titles.
    layout->setContentsMargins(settings::rowPadding());
    if (!descriptor.label.isEmpty()) {
        auto *title = new QLabel(descriptor.label, this);
        title->setObjectName(QStringLiteral("subsectionLabel"));
        layout->addWidget(title);
    }
    if (!descriptor.help.isEmpty()) {
        auto *help = new QLabel(descriptor.help, this);
        help->setObjectName(QStringLiteral("rowDescription"));
        help->setWordWrap(true);
        layout->addWidget(help);
    }

    QStringList titles;
    titles.reserve(m_collection.columns.size());
    for (const CollectionColumn &column : m_collection.columns) {
        titles.append(column.title);
    }
    m_table->setObjectName(descriptor.id);
    m_table->setColumnCount(titles.size());
    m_table->setHorizontalHeaderLabels(titles);
    for (int column = 0; column < m_collection.columns.size(); ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(
            column,
            m_collection.columns.at(column).stretch ? QHeaderView::Stretch
                                                    : QHeaderView::ResizeToContents);
    }
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Extended, not single: deleting a batch of learned corrections or imported
    // vocabulary one row at a time is the slowest way to use this editor.
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setMinimumHeight(m_collection.minimumHeight);
    if (!m_collection.emptyTitle.isEmpty()) {
        // What an empty table says, centred in it, as a list view's
        // placeholder would be.
        m_empty = new QLabel(m_table->viewport());
        m_empty->setObjectName(QStringLiteral("collectionEmpty"));
        m_empty->setTextFormat(Qt::PlainText);
        m_empty->setText(m_collection.emptyTitle + QLatin1Char('\n') + m_collection.emptyHelp);
        m_empty->setAlignment(Qt::AlignCenter);
        m_empty->setWordWrap(true);
        m_empty->setForegroundRole(QPalette::PlaceholderText);
        auto *emptyLayout = new QVBoxLayout(m_table->viewport());
        emptyLayout->addWidget(m_empty);
    }
    if (m_collection.badges) {
        for (int column = 0; column < m_collection.columns.size(); ++column) {
            if (m_collection.columns.at(column).stretch) {
                m_table->setItemDelegateForColumn(column, new BadgeDelegate(m_table));
            }
        }
    }
    m_delete->setObjectName(buttonObjectName(QStringLiteral("delete"), descriptor.id));
    m_delete->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    if (m_collection.supportsImport.parse) {
        m_import = new QPushButton(m_collection.supportsImport.actionLabel, this);
        m_import->setObjectName(buttonObjectName(QStringLiteral("import"), descriptor.id));
        connect(m_import, &QPushButton::clicked, this, [this] { importRecords(); });
        buttons->addWidget(m_import);
    }
    buttons->addStretch();
    for (const RowOption &action : m_collection.actions) {
        auto *button = new QPushButton(action.label, this);
        button->setObjectName(buttonObjectName(action.id, descriptor.id));
        connect(button, &QPushButton::clicked, this, [this, id = action.id] { runAction(id); });
        m_actions.insert(action.id, button);
        buttons->addWidget(button);
    }
    buttons->addWidget(m_delete);
    if (!m_collection.addLabel.isEmpty()) {
        m_add = new QPushButton(m_collection.addLabel, this);
        m_add->setObjectName(buttonObjectName(QStringLiteral("add"), descriptor.id));
        buttons->addWidget(m_add);
    }
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    connect(m_table, &QTableWidget::itemChanged, this, [this] { m_notifyChanged(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    // Return, Enter and a double-click activate a row, or a single click where
    // the style activates on one. A toggle's cell is its box instead.
    connect(m_table, &QTableWidget::cellActivated, this, [this](int row, int column) {
        if (m_collection.columns.at(column).kind != ColumnKind::Toggle) {
            editRecord(row);
        }
    });
    auto *editShortcut = new QShortcut(QKeySequence(Qt::Key_F2), m_table);
    editShortcut->setContext(Qt::WidgetShortcut);
    connect(editShortcut, &QShortcut::activated, this, [this] {
        if (m_table->currentRow() >= 0) {
            editRecord(m_table->currentRow());
        }
    });
    if (m_add) {
        connect(m_add, &QPushButton::clicked, this, [this] { editRecord(-1); });
    }
    connect(m_delete, &QPushButton::clicked, this, [this] {
        QList<int> rows = selectedEditableRows();
        if (rows.isEmpty()) {
            return;
        }
        // Descending, so removing a row cannot renumber the ones still to go.
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        const QList<QVariantMap> current = records();
        for (const int row : rows) {
            m_deleted.append({row - m_lockedCount, current.at(row - m_lockedCount)});
            m_table->removeRow(row);
        }
        updateButtons();
        m_notifyChanged();
    });
    updateButtons();
}

void CollectionEditor::runAction(const QString &actionId)
{
    QList<QVariantMap> current = records();
    if (actionId == QStringLiteral("undoDelete")) {
        if (m_deleted.isEmpty()) {
            return;
        }
        const auto [index, record] = m_deleted.takeLast();
        current.insert(std::min(index, current.size()), record);
    } else if (actionId == QStringLiteral("undoLatestLearn")) {
        if (current.isEmpty()) {
            return;
        }
        m_deleted.append({0, current.takeFirst()});
    } else {
        qFatal("the Qt collection editor has no command %s", qPrintable(actionId));
    }
    showRecords(lockedRecords() + current);
    m_notifyChanged();
}

void CollectionEditor::editRecord(int row)
{
    if (!m_editable || (row >= 0 && row < m_lockedCount)) {
        return;
    }
    openRecordDialog(this, m_collection, m_settings, row < 0 ? -1 : row - m_lockedCount,
                     [this] { return records(); },
                     [this](const QList<QVariantMap> &records) {
                         showRecords(lockedRecords() + records);
                         m_notifyChanged();
                     });
}

void CollectionEditor::importRecords()
{
    const std::optional<QList<QVariantMap>> merged =
        importedRecords(this, m_collection, records());
    if (!merged) {
        return;
    }
    showRecords(lockedRecords() + *merged);
    m_notifyChanged();
}

void CollectionEditor::appendRecord(const QVariantMap &record, bool locked)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *carrier = new QTableWidgetItem;
    carrier->setData(Qt::UserRole, record);
    m_table->setVerticalHeaderItem(row, carrier);
    for (int index = 0; index < m_collection.columns.size(); ++index) {
        const CollectionColumn &column = m_collection.columns.at(index);
        const QVariant value = record.value(column.id);
        const QString tooltip =
            column.recordTooltip ? column.recordTooltip(record) : column.tooltip;
        if (column.kind == ColumnKind::Toggle && !locked) {
            auto *item = new QTableWidgetItem;
            Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
            if (m_editable) {
                flags |= Qt::ItemIsUserCheckable;
            }
            item->setFlags(flags);
            item->setCheckState(value.toBool() ? Qt::Checked : Qt::Unchecked);
            m_table->setItem(row, index, item);
            continue;
        }
        if (column.kind == ColumnKind::Choice && !locked && m_editable) {
            auto *combo = new QComboBox(m_table);
            for (const RowOption &option : column.options(m_settings)) {
                combo->addItem(option.label, option.id);
            }
            settings::selectData(combo, value.toString());
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { m_notifyChanged(); });
            m_table->setCellWidget(row, index, combo);
            continue;
        }
        // Text is edited in the record dialog, so its cells only show it.
        QTableWidgetItem *item = readOnlyItem(column.kind == ColumnKind::Choice
                                                  ? optionLabel(column, value.toString(), m_settings)
                                                  : value.toString());
        item->setToolTip(tooltip);
        m_table->setItem(row, index, item);
    }
}

QList<QVariantMap> CollectionEditor::records() const
{
    QList<QVariantMap> records;
    for (int row = m_lockedCount; row < m_table->rowCount(); ++row) {
        // Start from what the row arrived with, so the keys no column shows
        // survive an edit to the ones that do.
        QVariantMap record = rowRecord(m_table, row);
        for (int index = 0; index < m_collection.columns.size(); ++index) {
            const CollectionColumn &column = m_collection.columns.at(index);
            if (column.kind == ColumnKind::Choice) {
                if (const auto *combo = qobject_cast<QComboBox *>(m_table->cellWidget(row, index))) {
                    record.insert(column.id, combo->currentData().toString());
                }
                continue;
            }
            const QTableWidgetItem *item = m_table->item(row, index);
            if (!item) {
                continue;
            }
            record.insert(column.id,
                          column.kind == ColumnKind::Toggle
                              ? QVariant(item->checkState() == Qt::Checked)
                              : QVariant(item->text()));
        }
        records.append(record);
    }
    return records;
}

void CollectionEditor::setRecords(const QList<QVariantMap> &records)
{
    // The editor's own edit comes straight back: announcing a change makes
    // SettingsPageSet reload every page from the draft, including this one.
    // Resetting on that echo would destroy the deletion history (and disable
    // "Undo delete") before the click handler even returns.
    if (records == lockedRecords() + this->records()) {
        return;
    }
    // A genuinely different set means a reload committed whatever Delete
    // took, so there is nothing left to undo.
    m_deleted.clear();
    showRecords(records);
}

void CollectionEditor::showRecords(const QList<QVariantMap> &records)
{
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    for (int index = 0; index < records.size(); ++index) {
        appendRecord(records.at(index), index < m_lockedCount);
    }
    m_table->clearSelection();
    updateButtons();
}

void CollectionEditor::refresh(const AppSettings &settings)
{
    m_settings = settings;
    if (!m_collection.badges) {
        return;
    }
    const auto stretch = std::find_if(m_collection.columns.cbegin(), m_collection.columns.cend(),
                                      [](const CollectionColumn &column) { return column.stretch; });
    const int column = int(stretch - m_collection.columns.cbegin());
    const QStringList badges = m_collection.badges(lockedRecords() + records(), settings);
    // Item data, not text, so it is no edit: nothing announces a change.
    const QSignalBlocker blocker(m_table);
    for (int row = 0; row < m_table->rowCount() && row < badges.size(); ++row) {
        if (QTableWidgetItem *item = m_table->item(row, column)) {
            item->setData(BadgeDelegate::TextRole, badges.at(row));
            item->setData(BadgeDelegate::ToneRole, int(Badge::Tone::Accent));
        }
    }
}

void CollectionEditor::setEditable(bool editable)
{
    if (m_editable == editable) {
        return;
    }
    // Read before the flag flips: records() reads the cells as they are.
    const QList<QVariantMap> shown = lockedRecords() + records();
    m_editable = editable;
    showRecords(shown);
}

QList<QVariantMap> CollectionEditor::lockedRecords() const
{
    QList<QVariantMap> locked;
    for (int row = 0; row < m_lockedCount; ++row) {
        locked.append(rowRecord(m_table, row));
    }
    return locked;
}

QList<int> CollectionEditor::selectedEditableRows() const
{
    QList<int> rows;
    const QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
    for (const QTableWidgetSelectionRange &range : ranges) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            if (row >= m_lockedCount && !rows.contains(row)) {
                rows.append(row);
            }
        }
    }
    return rows;
}

void CollectionEditor::updateButtons()
{
    if (m_empty) {
        m_empty->setVisible(m_table->rowCount() == 0);
    }
    m_delete->setEnabled(m_editable && !selectedEditableRows().isEmpty());
    if (m_add) {
        m_add->setEnabled(m_editable);
    }
    if (m_import) {
        m_import->setEnabled(m_editable);
    }
    if (QPushButton *undoDelete = m_actions.value(QStringLiteral("undoDelete"))) {
        undoDelete->setVisible(m_editable && !m_deleted.isEmpty());
    }
    if (QPushButton *undoLatestLearn = m_actions.value(QStringLiteral("undoLatestLearn"))) {
        undoLatestLearn->setEnabled(m_editable && m_table->rowCount() > m_lockedCount);
    }
}

} // namespace

std::optional<QList<QVariantMap>> importedRecords(QWidget *parent,
                                                  const CollectionDescriptor &collection,
                                                  const QList<QVariantMap> &current)
{
    const CollectionImport &source = collection.supportsImport;
    const auto refuse = [parent, &source](const QString &message) {
        QMessageBox::warning(parent, source.failureTitle, message);
        return std::optional<QList<QVariantMap>>();
    };
    const QString path =
        QFileDialog::getOpenFileName(parent, source.actionLabel, QString(), source.fileFilter);
    if (path.isEmpty()) {
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return refuse(QStringLiteral("Could not read %1.").arg(path));
    }
    QString error;
    const QList<QVariantMap> imported = source.parse(file.readAll(), &error);
    if (!error.isEmpty()) {
        return refuse(error);
    }
    const QList<QVariantMap> merged = current + imported;
    if (collection.validate) {
        const QStringList problems = collection.validate(merged);
        if (!problems.isEmpty()) {
            return refuse(problems.join(QLatin1Char('\n')));
        }
    }
    return merged;
}

void useMultilineEditor(QTableWidget *table, int column)
{
    table->setItemDelegateForColumn(column, new MultilineDelegate(table));
}

void openRecordDialog(QWidget *parent,
                      const CollectionDescriptor &collection,
                      const AppSettings &appSettings,
                      qsizetype row,
                      std::function<QList<QVariantMap>()> current,
                      std::function<void(const QList<QVariantMap> &)> apply)
{
    const QVariantMap original = row < 0 ? collection.blankRecord : current().at(row);
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("collectionRecordDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(row < 0 ? collection.addDialogTitle : recordName(collection, original));
    dialog->setMinimumWidth(settings::gridUnit() * 30);
    auto *layout = new QVBoxLayout(dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form, 1);

    // Each field writes its own column into the record the dialog hands back.
    QList<std::function<void(QVariantMap &)>> readers;
    QWidget *firstText = nullptr;
    for (const CollectionColumn &column : collection.columns) {
        const QVariant value = original.value(column.id);
        QWidget *field = nullptr;
        if (column.kind == ColumnKind::Toggle) {
            auto *box = new QCheckBox(column.title, dialog);
            box->setChecked(value.toBool());
            form->addRow(box);
            readers.append([box, id = column.id](QVariantMap &record) {
                record.insert(id, box->isChecked());
            });
            field = box;
        } else if (column.kind == ColumnKind::Choice) {
            auto *combo = new QComboBox(dialog);
            for (const RowOption &option : column.options(appSettings)) {
                combo->addItem(option.label, option.id);
            }
            settings::selectData(combo, value.toString());
            form->addRow(column.title, combo);
            readers.append([combo, id = column.id](QVariantMap &record) {
                record.insert(id, combo->currentData().toString());
            });
            field = combo;
        } else if (column.kind == ColumnKind::Text && column.multiline) {
            auto *edit = new QPlainTextEdit(value.toString(), dialog);
            // Return starts a new line, so Tab is what moves on.
            edit->setTabChangesFocus(true);
            form->addRow(column.title, edit);
            readers.append([edit, id = column.id](QVariantMap &record) {
                record.insert(id, edit->toPlainText());
            });
            field = edit;
        } else if (column.kind == ColumnKind::Text) {
            auto *edit = new QLineEdit(value.toString(), dialog);
            form->addRow(column.title, edit);
            readers.append([edit, id = column.id](QVariantMap &record) {
                record.insert(id, edit->text().trimmed());
            });
            field = edit;
        }
        if (field) {
            field->setObjectName(column.id);
        }
        if (!firstText && column.kind == ColumnKind::Text) {
            firstText = field;
        }
    }

    auto *problems = new InlineMessage(dialog);
    problems->setType(InlineMessage::Type::Error);
    problems->setCloseButtonVisible(false);
    problems->hide();
    layout->addWidget(problems);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog,
                     [dialog, problems, readers, original, row, collection, current, apply] {
        QVariantMap record = original;
        for (const auto &read : readers) {
            read(record);
        }
        // Found again rather than taken by index: the records may have been
        // reloaded while the dialog was open.
        QList<QVariantMap> records = current();
        const qsizetype index = row < 0 ? -1 : records.indexOf(original);
        if (index < 0) {
            records.append(record);
        } else {
            records[index] = record;
        }
        const QStringList refused = collection.validate ? collection.validate(records) : QStringList();
        if (!refused.isEmpty()) {
            problems->setText(refused.join(QLatin1Char('\n')));
            problems->show();
            return;
        }
        apply(records);
        dialog->accept();
    });
    if (firstText) {
        firstText->setFocus(Qt::OtherFocusReason);
    }
    dialog->open();
}

SchemaCustomRow makeCollectionRow(const SettingsRow &descriptor,
                                  QWidget *parent,
                                  std::function<void()> notifyChanged)
{
    auto *editor = new CollectionEditor(descriptor, parent, std::move(notifyChanged));
    return {
        editor,
        [editor] { return QVariant::fromValue(editor->records()); },
        [editor](const QVariant &value) { editor->setRecords(value.value<QList<QVariantMap>>()); },
        true,
        nullptr,
        [editor](const AppSettings &settings) { editor->refresh(settings); },
        [editor](bool editable) { editor->setEditable(editable); },
    };
}

} // namespace speecher
