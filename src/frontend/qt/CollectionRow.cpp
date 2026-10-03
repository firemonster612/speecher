#include "frontend/qt/CollectionRow.h"

#include "ui/InlineMessage.h"
#include "ui/InsightsCharts.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QPainter>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

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

// An Icon column's icon from the theme's monochrome set, by the schema's
// platform-neutral id.
QIcon collectionIcon(const QString &iconId)
{
    static const QHash<QString, QStringList> names{
        {QStringLiteral("microphone"), {QStringLiteral("audio-input-microphone")}},
        {QStringLiteral("star"), {QStringLiteral("starred"), QStringLiteral("rating"), QStringLiteral("emblem-favorite")}},
    };
    for (const QString &name : names.value(iconId)) {
        if (QIcon::hasThemeIcon(name)) {
            return QIcon::fromTheme(name);
        }
    }
    return {};
}

// An Icon column's cell: the style draws the cell, selection included, and
// the icon sits centred in it, where an item view would put it at the left.
class IconCellDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem item = option;
        initStyleOption(&item, index);
        const QIcon icon = item.icon;
        item.icon = QIcon();
        item.features &= ~QStyleOptionViewItem::HasDecoration;
        const QWidget *widget = option.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);
        icon.paint(painter, item.rect, Qt::AlignCenter,
                   item.state & QStyle::State_Selected ? QIcon::Selected : QIcon::Normal);
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

// What a record's dialog is titled after: its first text column, which names
// it, read-only or not.
QString recordName(const CollectionDescriptor &collection, const QVariantMap &record)
{
    for (const CollectionColumn &column : collection.columns) {
        if (column.kind == ColumnKind::Text || column.kind == ColumnKind::ReadOnly) {
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
    // Says each ChoiceSet cell again, for the options the settings now offer.
    void showChoiceSets();
    // Draws each Icon cell again, for the records as they now stand.
    void showIcons();
    void updateButtons();
    bool eventFilter(QObject *watched, QEvent *event) override;

    CollectionDescriptor m_collection;
    // The columns the table shows; the record dialog fills in every column.
    QList<CollectionColumn> m_columns;
    AppSettings m_settings;
    QTableWidget *m_table;
    QPushButton *m_add = nullptr;
    QPushButton *m_edit = nullptr;
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
    for (const CollectionColumn &column : m_collection.columns) {
        if (!column.dialogOnly) {
            m_columns.append(column);
            titles.append(column.title);
        }
    }
    m_table->setObjectName(descriptor.id);
    m_table->setColumnCount(titles.size());
    m_table->setHorizontalHeaderLabels(titles);
    QHeaderView *header = m_table->horizontalHeader();
    for (int column = 0; column < m_columns.size(); ++column) {
        const CollectionColumn &shown = m_columns.at(column);
        if (shown.kind == ColumnKind::Icon) {
            // The icon names the column; its title is the tooltip and what a
            // screen reader says.
            auto *title = new QTableWidgetItem(collectionIcon(shown.iconId), QString());
            title->setToolTip(shown.title);
            title->setData(Qt::AccessibleTextRole, shown.title);
            m_table->setHorizontalHeaderItem(column, title);
            // As wide as its icon and the header's margins, no wider.
            const int width = m_table->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, m_table)
                + 2 * m_table->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, header);
            header->setMinimumSectionSize(std::min(header->minimumSectionSize(), width));
            header->setSectionResizeMode(column, QHeaderView::Fixed);
            header->resizeSection(column, width);
            m_table->setItemDelegateForColumn(column, new IconCellDelegate(m_table));
            continue;
        }
        if (m_columns.at(column).kind == ColumnKind::ChoiceSet) {
            // It can name every option, so it takes the width of its widest
            // one and elides a longer list, leaving the room to the stretch
            // column; the tooltip holds the whole list.
            int widest = header->sectionSizeHint(column);
            for (const RowOption &option : m_columns.at(column).options(AppSettings())) {
                widest = std::max(widest, m_table->fontMetrics().horizontalAdvance(option.label)
                                              + 2 * (style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1)
                                              + m_table->style()->pixelMetric(QStyle::PM_HeaderMargin));
            }
            header->setSectionResizeMode(column, QHeaderView::Interactive);
            header->resizeSection(column, widest);
            continue;
        }
        header->setSectionResizeMode(
            column,
            m_columns.at(column).stretch ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
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
        m_empty->setText(m_collection.emptyHelp.isEmpty()
                             ? m_collection.emptyTitle
                             : m_collection.emptyTitle + QLatin1Char('\n') + m_collection.emptyHelp);
        m_empty->setAlignment(Qt::AlignCenter);
        m_empty->setWordWrap(true);
        m_empty->setForegroundRole(QPalette::PlaceholderText);
        auto *emptyLayout = new QVBoxLayout(m_table->viewport());
        emptyLayout->addWidget(m_empty);
    }
    bool detailed = false;
    for (int column = 0; column < m_columns.size(); ++column) {
        const CollectionColumn &shown = m_columns.at(column);
        if (!shown.detailColumn.isEmpty()) {
            m_table->setItemDelegateForColumn(column, new BadgeDelegate(m_table));
        }
        detailed = detailed || !shown.detailColumn.isEmpty();
    }
    if (detailed) {
        // Room for the detail's line under every record's own, which other
        // cells must not wrap into.
        m_table->setWordWrap(false);
        m_table->verticalHeader()->setDefaultSectionSize(
            m_table->verticalHeader()->defaultSectionSize() + m_table->fontMetrics().height());
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
    if (!m_collection.editLabel.isEmpty()) {
        m_edit = new QPushButton(m_collection.editLabel, this);
        m_edit->setObjectName(buttonObjectName(QStringLiteral("edit"), descriptor.id));
        connect(m_edit, &QPushButton::clicked, this, [this] {
            const QList<int> rows = selectedEditableRows();
            if (rows.size() == 1) {
                editRecord(rows.first());
            }
        });
        buttons->addWidget(m_edit);
    }
    if (!m_collection.addLabel.isEmpty()) {
        m_add = new QPushButton(m_collection.addLabel, this);
        m_add->setObjectName(buttonObjectName(QStringLiteral("add"), descriptor.id));
        buttons->addWidget(m_add);
    }
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    connect(m_table, &QTableWidget::itemChanged, this, [this] { m_notifyChanged(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    // A double-click activates a row, or a single click where the style
    // activates on one. A toggle's cell is its box instead.
    connect(m_table, &QTableWidget::cellActivated, this, [this](int row, int column) {
        if (m_columns.at(column).kind != ColumnKind::Toggle) {
            editRecord(row);
        }
    });
    // Return, Enter and F2 open a row through eventFilter.
    m_table->installEventFilter(this);
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

// macOS's item views take Return as an edit key rather than activation, so
// the keys that open a row are handled here, ahead of the view.
bool CollectionEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_table || event->type() != QEvent::KeyPress || m_table->currentRow() < 0) {
        return QWidget::eventFilter(watched, event);
    }
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_F2) {
        return QWidget::eventFilter(watched, event);
    }
    editRecord(m_table->currentRow());
    return true;
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
    for (int index = 0; index < m_columns.size(); ++index) {
        const CollectionColumn &column = m_columns.at(index);
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
            item->setToolTip(tooltip);
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
        // Its icon and tooltip come from showIcons().
        if (column.kind == ColumnKind::Icon) {
            m_table->setItem(row, index, readOnlyItem(QString()));
            continue;
        }
        // Text is edited in the record dialog, so its cells only show it.
        QString text = value.toString();
        if (column.kind == ColumnKind::Choice) {
            text = optionLabel(column, text, m_settings);
        } else if (column.kind == ColumnKind::ChoiceSet) {
            text = choiceSetText(column, value.toStringList(), m_settings);
        }
        QTableWidgetItem *item = readOnlyItem(text);
        item->setToolTip(column.kind == ColumnKind::ChoiceSet ? text : tooltip);
        if (!column.detailColumn.isEmpty()) {
            const QString detail = record.value(column.detailColumn).toString();
            item->setData(BadgeDelegate::DetailRole, detail);
            if (!detail.isEmpty()) {
                item->setToolTip(detail);
            }
        }
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
        for (int index = 0; index < m_columns.size(); ++index) {
            const CollectionColumn &column = m_columns.at(index);
            if (column.kind == ColumnKind::Choice) {
                if (const auto *combo = qobject_cast<QComboBox *>(m_table->cellWidget(row, index))) {
                    record.insert(column.id, combo->currentData().toString());
                }
                continue;
            }
            // Its cell only names the options; the ids stay in the record.
            // An Icon cell holds nothing of the record's.
            if (column.kind == ColumnKind::ChoiceSet || column.kind == ColumnKind::Icon) {
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
    showChoiceSets();
    showIcons();
}

void CollectionEditor::showIcons()
{
    const QList<QVariantMap> shown = lockedRecords() + records();
    // Item data, not text, so it is no edit: nothing announces a change.
    const QSignalBlocker blocker(m_table);
    const int size = m_table->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, m_table);
    for (int index = 0; index < m_columns.size(); ++index) {
        const CollectionColumn &column = m_columns.at(index);
        if (column.kind != ColumnKind::Icon || !column.icons) {
            continue;
        }
        const QIcon icon = collectionIcon(column.iconId);
        // Faint is the style's own disabled look of the icon.
        const QIcon faint(icon.pixmap(QSize(size, size), m_table->devicePixelRatio(), QIcon::Disabled));
        const QList<IconCell> cells = column.icons(shown, m_settings);
        for (int row = 0; row < m_table->rowCount() && row < cells.size(); ++row) {
            QTableWidgetItem *item = m_table->item(row, index);
            if (!item) {
                continue;
            }
            const IconCell &cell = cells.at(row);
            item->setIcon(cell.state == IconCell::State::Shown   ? icon
                          : cell.state == IconCell::State::Faint ? faint
                                                                 : QIcon());
            item->setToolTip(cell.tooltip);
        }
    }
}

void CollectionEditor::showChoiceSets()
{
    const QSignalBlocker blocker(m_table);
    for (int index = 0; index < m_columns.size(); ++index) {
        const CollectionColumn &column = m_columns.at(index);
        if (column.kind != ColumnKind::ChoiceSet) {
            continue;
        }
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (QTableWidgetItem *item = m_table->item(row, index)) {
                const QString text = choiceSetText(column, rowRecord(m_table, row).value(column.id).toStringList(),
                                                   m_settings);
                item->setText(text);
                item->setToolTip(text);
            }
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
    if (m_edit) {
        m_edit->setEnabled(m_editable && selectedEditableRows().size() == 1);
    }
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
                      std::function<void(const QList<QVariantMap> &)> apply,
                      std::function<void()> remove,
                      const QString &removalNotice)
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
    // What must hold before OK takes the record, rechecked as fields change.
    QList<std::function<bool()>> checks;
    const auto recheck = std::make_shared<std::function<void()>>();
    // The checkboxes so far, by column, for a later one enabled by them.
    QHash<QString, QCheckBox *> toggles;
    QWidget *firstText = nullptr;
    // A field with help gets it underneath, in one widget with the field: a
    // wrapped label as a row of its own is sized too narrow and clipped.
    // A checkbox names itself, so its row spans the form with no title.
    const auto addField = [dialog, form](const CollectionColumn &column, QWidget *field) {
        const bool titled = column.kind != ColumnKind::Toggle;
        if (column.help.isEmpty()) {
            titled ? form->addRow(column.title, field) : form->addRow(field);
            return;
        }
        auto *withHelp = new QWidget(dialog);
        withHelp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *layout = new QVBoxLayout(withHelp);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(settings::tightSpacing());
        auto *help = new QLabel(column.help, withHelp);
        help->setObjectName(column.id + QStringLiteral("Help"));
        help->setWordWrap(true);
        help->setForegroundRole(QPalette::PlaceholderText);
        help->setFont(settings::smallFont(help->font()));
        if (!titled) {
            // In line with the checkbox's text, past its box.
            QStyle *style = field->style();
            help->setContentsMargins(style->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, field)
                                         + style->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, field),
                                     0, 0, 0);
        }
        layout->addWidget(field);
        layout->addWidget(help);
        titled ? form->addRow(column.title, withHelp) : form->addRow(withHelp);
    };
    for (const CollectionColumn &column : collection.columns) {
        const QVariant value = original.value(column.id);
        QWidget *field = nullptr;
        if (column.kind == ColumnKind::Toggle) {
            auto *box = new QCheckBox(column.title, dialog);
            box->setChecked(value.toBool());
            if (QCheckBox *enabler = toggles.value(column.enabledBy)) {
                box->setEnabled(enabler->isChecked());
                QObject::connect(enabler, &QCheckBox::toggled, box, &QWidget::setEnabled);
            }
            toggles.insert(column.id, box);
            addField(column, box);
            readers.append([box, id = column.id](QVariantMap &record) {
                record.insert(id, box->isChecked());
            });
            field = box;
        } else if (column.kind == ColumnKind::Choice) {
            auto *combo = new QComboBox(dialog);
            const QList<RowOption> options = column.options(appSettings);
            for (const RowOption &option : options) {
                combo->addItem(option.label, option.id);
            }
            settings::selectData(combo, value.toString());
            // What the chosen option does, under it, where the options say.
            if (std::any_of(options.cbegin(), options.cend(),
                            [](const RowOption &option) { return !option.help.isEmpty(); })) {
                // The help takes the field's width; the combo keeps its own.
                auto *choice = new QWidget(dialog);
                choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                auto *choiceLayout = new QVBoxLayout(choice);
                choiceLayout->setContentsMargins(0, 0, 0, 0);
                choiceLayout->setSpacing(settings::tightSpacing());
                auto *help = new QLabel(choice);
                help->setObjectName(column.id + QStringLiteral("Help"));
                help->setWordWrap(true);
                help->setForegroundRole(QPalette::PlaceholderText);
                help->setFont(settings::smallFont(help->font()));
                const auto showHelp = [combo, help, options] {
                    help->setText(options.value(combo->currentIndex()).help);
                };
                showHelp();
                QObject::connect(combo, &QComboBox::currentIndexChanged, help, showHelp);
                choiceLayout->addWidget(combo, 0, Qt::AlignLeft);
                choiceLayout->addWidget(help);
                form->addRow(column.title, choice);
            } else {
                addField(column, combo);
            }
            readers.append([combo, id = column.id](QVariantMap &record) {
                record.insert(id, combo->currentData().toString());
            });
            field = combo;
        } else if (column.kind == ColumnKind::ChoiceSet) {
            auto *choice = new QWidget(dialog);
            auto *choiceLayout = new QVBoxLayout(choice);
            choiceLayout->setContentsMargins(0, 0, 0, 0);
            choiceLayout->setSpacing(settings::tightSpacing());
            auto *every = new QRadioButton(column.everyChoice, choice);
            auto *some = new QRadioButton(column.someChoice, choice);
            auto *list = new QListWidget(choice);
            list->setObjectName(column.id + QStringLiteral("Options"));
            const QStringList chosen = value.toStringList();
            bool limited = false;
            for (const RowOption &option : column.options(appSettings)) {
                auto *item = new QListWidgetItem(option.label, list);
                item->setData(Qt::UserRole, option.id);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                item->setCheckState(chosen.contains(option.id) ? Qt::Checked : Qt::Unchecked);
                limited = limited || chosen.contains(option.id);
            }
            // Every option in view where the screen has room; on a short one
            // the list scrolls, still showing a few.
            const int rows = list->sizeHintForRow(0);
            list->setMinimumHeight(rows * std::min(3, int(list->count())) + 2 * list->frameWidth());
            list->setMaximumHeight(rows * list->count() + 2 * list->frameWidth());
            (limited ? some : every)->setChecked(true);
            list->setEnabled(limited);
            QObject::connect(some, &QRadioButton::toggled, list, &QWidget::setEnabled);
            choiceLayout->addWidget(every);
            choiceLayout->addWidget(some);
            choiceLayout->addWidget(list);
            addField(column, choice);
            const auto ticked = [list] {
                QStringList ids;
                for (int index = 0; index < list->count(); ++index) {
                    if (list->item(index)->checkState() == Qt::Checked) {
                        ids.append(list->item(index)->data(Qt::UserRole).toString());
                    }
                }
                return ids;
            };
            readers.append([some, ticked, id = column.id](QVariantMap &record) {
                record.insert(id, some->isChecked() ? ticked() : QStringList());
            });
            checks.append([some, ticked] { return !some->isChecked() || !ticked().isEmpty(); });
            QObject::connect(some, &QRadioButton::toggled, dialog, [recheck] { (*recheck)(); });
            QObject::connect(list, &QListWidget::itemChanged, dialog, [recheck] { (*recheck)(); });
            field = choice;
        } else if (column.kind == ColumnKind::Text && column.multiline) {
            auto *edit = new QPlainTextEdit(value.toString(), dialog);
            edit->setPlaceholderText(column.placeholder);
            // Return starts a new line, so Tab is what moves on.
            edit->setTabChangesFocus(true);
            addField(column, edit);
            readers.append([edit, id = column.id](QVariantMap &record) {
                record.insert(id, edit->toPlainText());
            });
            field = edit;
        } else if (column.kind == ColumnKind::Text) {
            auto *edit = new QLineEdit(value.toString(), dialog);
            edit->setPlaceholderText(column.placeholder);
            addField(column, edit);
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
    if (remove) {
        // What deleting changes is said beside the button that does it, not
        // in a second dialog.
        auto *caution = new InlineMessage(dialog);
        caution->setType(InlineMessage::Type::Warning);
        caution->setCloseButtonVisible(false);
        caution->setText(removalNotice);
        auto *confirm = new QPushButton(collection.deleteLabel, caution);
        confirm->setObjectName(QStringLiteral("confirmDeleteRecord"));
        caution->addAction(confirm);
        caution->hide();
        layout->insertWidget(layout->indexOf(problems), caution);
        const auto removeAndClose = [dialog, remove] {
            remove();
            dialog->accept();
        };
        QObject::connect(confirm, &QPushButton::clicked, dialog, removeAndClose);
        QPushButton *deleteButton = buttons->addButton(collection.deleteLabel, QDialogButtonBox::DestructiveRole);
        deleteButton->setObjectName(QStringLiteral("deleteRecord"));
        QObject::connect(deleteButton, &QPushButton::clicked, dialog, [caution, removalNotice, removeAndClose] {
            if (removalNotice.isEmpty()) {
                removeAndClose();
            } else {
                caution->show();
            }
        });
    }
    layout->addWidget(buttons);
    // The first text field names the record, so there is nothing to keep
    // until it holds something.
    if (auto *name = qobject_cast<QLineEdit *>(firstText)) {
        checks.append([name] { return !name->text().trimmed().isEmpty(); });
        QObject::connect(name, &QLineEdit::textChanged, dialog, [recheck] { (*recheck)(); });
    }
    *recheck = [ok = buttons->button(QDialogButtonBox::Ok), checks] {
        ok->setEnabled(std::all_of(checks.cbegin(), checks.cend(), [](const auto &check) { return check(); }));
    };
    (*recheck)();

    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog,
                     [dialog, problems, readers, original, row, collection, current, apply] {
        QVariantMap record = original;
        for (const auto &read : readers) {
            read(record);
        }
        const auto refuse = [problems](const QString &message) {
            problems->setText(message);
            problems->show();
        };
        // Found again rather than taken by index: the records may have been
        // reloaded while the dialog was open, and one that changed or went
        // must not come back as a second copy.
        QList<QVariantMap> records = current();
        if (row < 0) {
            records.append(record);
        } else if (const qsizetype index = records.indexOf(original); index >= 0) {
            records[index] = record;
        } else {
            refuse(QStringLiteral("%1 changed while this dialog was open. Cancel and edit it again.")
                       .arg(recordName(collection, original)));
            return;
        }
        const QStringList refused = collection.validate ? collection.validate(records) : QStringList();
        if (!refused.isEmpty()) {
            refuse(refused.join(QLatin1Char('\n')));
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
