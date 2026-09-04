#include "frontend/qt/CollectionRow.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace speecher {

namespace {

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    return item;
}

QString optionLabel(const CollectionColumn &column, const QString &id)
{
    if (!column.options) {
        return id;
    }
    for (const RowOption &option : column.options()) {
        if (option.id == id) {
            return option.label;
        }
    }
    return id;
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

private:
    void showRecords(const QList<QVariantMap> &records);
    void appendRecord(const QVariantMap &record, bool locked);
    QList<QVariantMap> lockedRecords() const;
    void importRecords();
    void runAction(const QString &actionId);
    QList<int> selectedEditableRows() const;
    void updateButtons();

    CollectionDescriptor m_collection;
    QTableWidget *m_table;
    QPushButton *m_add = nullptr;
    QPushButton *m_delete;
    QHash<QString, QPushButton *> m_actions;
    int m_lockedCount;
    // What Delete took, newest last, so undo can put it back.
    QList<QVariantMap> m_deleted;
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
    , m_delete(new QPushButton(QStringLiteral("Delete selected"), this))
    , m_lockedCount(m_collection.lockedRecordCount ? m_collection.lockedRecordCount() : 0)
    , m_notifyChanged(std::move(notifyChanged))
{
    // The row that holds the editor carries its title and padding.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

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
    m_table->setMinimumHeight(
        settings::collectionEditorMinimumHeight(m_table, m_collection.minimumVisibleRows));
    m_delete->setObjectName(buttonObjectName(QStringLiteral("delete"), descriptor.id));
    m_delete->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    if (m_collection.supportsImport.parse) {
        auto *import = new QPushButton(m_collection.supportsImport.actionLabel, this);
        import->setObjectName(buttonObjectName(QStringLiteral("import"), descriptor.id));
        connect(import, &QPushButton::clicked, this, [this] { importRecords(); });
        buttons->addWidget(import);
    }
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
    if (m_add) {
        connect(m_add, &QPushButton::clicked, this, [this] {
            appendRecord(m_collection.blankRecord, false);
            const int row = m_table->rowCount() - 1;
            m_table->selectRow(row);
            for (int column = 0; column < m_collection.columns.size(); ++column) {
                if (m_collection.columns.at(column).kind == ColumnKind::Text) {
                    m_table->setCurrentCell(row, column);
                    m_table->editItem(m_table->item(row, column));
                    break;
                }
            }
            updateButtons();
            m_notifyChanged();
        });
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
            m_deleted.append(current.at(row - m_lockedCount));
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
        current.prepend(m_deleted.takeLast());
    } else if (actionId == QStringLiteral("undoLatestLearn")) {
        if (current.isEmpty()) {
            return;
        }
        m_deleted.append(current.takeFirst());
    } else {
        qFatal("the Qt collection editor has no command %s", qPrintable(actionId));
    }
    showRecords(lockedRecords() + current);
    m_notifyChanged();
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
        if (locked || column.kind == ColumnKind::ReadOnly) {
            QTableWidgetItem *item = readOnlyItem(column.kind == ColumnKind::Choice
                                                      ? optionLabel(column, value.toString())
                                                      : value.toString());
            item->setToolTip(tooltip);
            m_table->setItem(row, index, item);
            continue;
        }
        if (column.kind == ColumnKind::Toggle) {
            auto *item = new QTableWidgetItem;
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(value.toBool() ? Qt::Checked : Qt::Unchecked);
            m_table->setItem(row, index, item);
            continue;
        }
        if (column.kind == ColumnKind::Choice) {
            auto *combo = new QComboBox(m_table);
            for (const RowOption &option : column.options()) {
                combo->addItem(option.label, option.id);
            }
            settings::selectData(combo, value.toString());
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { m_notifyChanged(); });
            m_table->setCellWidget(row, index, combo);
            continue;
        }
        auto *item = new QTableWidgetItem(value.toString());
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
    // A reload has committed whatever Delete took, so there is nothing to undo.
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
    m_delete->setEnabled(!selectedEditableRows().isEmpty());
    if (QPushButton *undoDelete = m_actions.value(QStringLiteral("undoDelete"))) {
        undoDelete->setEnabled(!m_deleted.isEmpty());
    }
    if (QPushButton *undoLatestLearn = m_actions.value(QStringLiteral("undoLatestLearn"))) {
        undoLatestLearn->setEnabled(m_table->rowCount() > m_lockedCount);
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
    };
}

} // namespace speecher
