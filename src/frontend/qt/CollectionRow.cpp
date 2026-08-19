#include "frontend/qt/CollectionRow.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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
    void setRecords(const QList<QVariantMap> &records);

private:
    void appendRecord(const QVariantMap &record, bool locked);
    void updateDeleteButton();

    CollectionDescriptor m_collection;
    QTableWidget *m_table;
    QPushButton *m_add;
    QPushButton *m_delete;
    int m_lockedCount;
    std::function<void()> m_notifyChanged;
};

CollectionEditor::CollectionEditor(const SettingsRow &descriptor,
                                   QWidget *parent,
                                   std::function<void()> notifyChanged)
    : QWidget(parent)
    , m_collection(descriptor.collection)
    , m_table(new QTableWidget(this))
    , m_add(new QPushButton(m_collection.addLabel, this))
    , m_delete(new QPushButton(QStringLiteral("Delete selected"), this))
    , m_lockedCount(m_collection.lockedRecordCount ? m_collection.lockedRecordCount() : 0)
    , m_notifyChanged(std::move(notifyChanged))
{
    auto *layout = new QVBoxLayout(this);
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
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setMinimumHeight(m_collection.minimumHeight);
    m_add->setObjectName(buttonObjectName(QStringLiteral("add"), descriptor.id));
    m_delete->setObjectName(buttonObjectName(QStringLiteral("delete"), descriptor.id));
    m_delete->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_delete);
    buttons->addWidget(m_add);
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    connect(m_table, &QTableWidget::itemChanged, this, [this] { m_notifyChanged(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { updateDeleteButton(); });
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
        m_notifyChanged();
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < m_lockedCount) {
            return;
        }
        m_table->removeRow(row);
        updateDeleteButton();
        m_notifyChanged();
    });
}

void CollectionEditor::appendRecord(const QVariantMap &record, bool locked)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    for (int index = 0; index < m_collection.columns.size(); ++index) {
        const CollectionColumn &column = m_collection.columns.at(index);
        const QVariant value = record.value(column.id);
        if (locked || column.kind == ColumnKind::ReadOnly) {
            m_table->setItem(row,
                             index,
                             readOnlyItem(column.kind == ColumnKind::Choice
                                              ? optionLabel(column, value.toString())
                                              : value.toString()));
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
        item->setToolTip(column.tooltip);
        m_table->setItem(row, index, item);
    }
}

QList<QVariantMap> CollectionEditor::records() const
{
    QList<QVariantMap> records;
    for (int row = m_lockedCount; row < m_table->rowCount(); ++row) {
        QVariantMap record;
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
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(0);
    for (int index = 0; index < records.size(); ++index) {
        appendRecord(records.at(index), index < m_lockedCount);
    }
    m_table->clearSelection();
    updateDeleteButton();
}

void CollectionEditor::updateDeleteButton()
{
    m_delete->setEnabled(m_table->currentRow() >= m_lockedCount);
}

} // namespace

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
