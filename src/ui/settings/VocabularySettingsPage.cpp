#include "ui/settings/VocabularySettingsPage.h"

#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace speecher {

class VocabularyTable : public QTableWidget {
public:
    explicit VocabularyTable(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        QTableWidget::keyPressEvent(event);
    }
};

static QTableWidgetItem *makeVocabularyItem(const QString &text = QString())
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
    return item;
}

VocabularySettingsPage::VocabularySettingsPage(QWidget *parent)
    : QFrame(parent)
    , m_table(new VocabularyTable(this))
    , m_limit(new QLabel(this))
    , m_addButton(new QPushButton(QStringLiteral("Add"), this))
    , m_importButton(new QPushButton(QStringLiteral("Import CSV"), this))
    , m_removeButton(new QPushButton(QStringLiteral("Delete selected"), this))
{
    setObjectName(QStringLiteral("vocabSection"));
    m_table->setObjectName(QStringLiteral("vocabInput"));
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Star"),
        QStringLiteral("Term"),
        QStringLiteral("Source"),
        QStringLiteral("Uses"),
        QStringLiteral("Last used"),
    });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_table->setTabKeyNavigation(false);
    m_table->setMinimumHeight(120);
    m_limit->setObjectName(QStringLiteral("statusText"));
    m_limit->setForegroundRole(QPalette::WindowText);
    m_limit->setAttribute(Qt::WA_StyledBackground, false);
    m_removeButton->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("Extra vocabulary"), this);
    title->setObjectName(QStringLiteral("subsectionLabel"));
    title->setForegroundRole(QPalette::WindowText);
    title->setAttribute(Qt::WA_StyledBackground, false);
    auto *description = new QLabel(QStringLiteral("One term per line. Claude voice uses Deepgram Nova-3 keyterms: 500 tokens and 100 keyterms maximum."), this);
    description->setObjectName(QStringLiteral("rowDescription"));
    description->setWordWrap(true);
    description->setAttribute(Qt::WA_StyledBackground, false);
    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(m_table);
    layout->addWidget(m_limit);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_importButton);
    buttons->addStretch();
    buttons->addWidget(m_removeButton);
    layout->addLayout(buttons);

    connect(m_table, &QTableWidget::itemChanged, this, [this] {
        updateLimit();
        emit changed();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        m_removeButton->setEnabled(!m_table->selectionModel()->selectedRows().isEmpty());
    });
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        addEntry();
        const int row = m_table->rowCount() - 1;
        m_table->setCurrentCell(row, 1);
        m_table->editItem(m_table->item(row, 1));
        emit changed();
    });
    connect(m_importButton, &QPushButton::clicked, this, &VocabularySettingsPage::importCsv);
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        QModelIndexList rows = m_table->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex &left, const QModelIndex &right) {
            return left.row() > right.row();
        });
        for (const QModelIndex &row : rows) {
            m_table->removeRow(row.row());
        }
        updateLimit();
        emit changed();
    });
}

void VocabularySettingsPage::load(const QList<VocabularyEntry> &entries)
{
    setRows(entries);
    updateLimit();
}

QList<VocabularyEntry> VocabularySettingsPage::entries() const
{
    QList<VocabularyEntry> result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *starred = m_table->item(row, 0);
        const QTableWidgetItem *term = m_table->item(row, 1);
        const QTableWidgetItem *source = m_table->item(row, 2);
        const QTableWidgetItem *frequency = m_table->item(row, 3);
        const QTableWidgetItem *lastUsed = m_table->item(row, 4);
        if (term && !term->text().trimmed().isEmpty()) {
            result.append({
                term->text(),
                source ? source->text() : QStringLiteral("manual"),
                starred && starred->checkState() == Qt::Checked,
                frequency ? frequency->data(Qt::UserRole).toInt() : 0,
                lastUsed ? lastUsed->data(Qt::UserRole).toLongLong() : 0,
            });
        }
    }
    return normalizeVocabularyEntries(result);
}

bool VocabularySettingsPage::hasChanges(const QList<VocabularyEntry> &entries) const
{
    return this->entries() != entries;
}

void VocabularySettingsPage::setRows(const QList<VocabularyEntry> &entries)
{
    QSignalBlocker blocker(m_table);
    m_updating = true;

    m_table->clearContents();
    m_table->setRowCount(0);
    for (const VocabularyEntry &entry : normalizeVocabularyEntries(entries)) {
        addEntry(entry);
    }
    if (m_table->rowCount() > 0) {
        m_table->setCurrentCell(0, 1);
    }

    m_updating = false;
}

void VocabularySettingsPage::addEntry(const VocabularyEntry &entry)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *starred = new QTableWidgetItem;
    starred->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    starred->setCheckState(entry.starred ? Qt::Checked : Qt::Unchecked);
    auto *term = makeVocabularyItem(entry.term);
    auto *source = makeVocabularyItem(entry.source.isEmpty() ? QStringLiteral("manual") : entry.source);
    auto *frequency = new QTableWidgetItem(QString::number(qMax(0, entry.frequency)));
    frequency->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    frequency->setData(Qt::UserRole, qMax(0, entry.frequency));
    auto *lastUsed = new QTableWidgetItem(
        entry.lastUsedMs > 0
            ? QLocale().toString(
                  QDateTime::fromMSecsSinceEpoch(entry.lastUsedMs),
                  QLocale::ShortFormat)
            : QStringLiteral("Never"));
    lastUsed->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    lastUsed->setData(Qt::UserRole, entry.lastUsedMs);
    m_table->setItem(row, 0, starred);
    m_table->setItem(row, 1, term);
    m_table->setItem(row, 2, source);
    m_table->setItem(row, 3, frequency);
    m_table->setItem(row, 4, lastUsed);
}

void VocabularySettingsPage::importCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import vocabulary"),
        QString(),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,
                             QStringLiteral("Vocabulary not imported"),
                             QStringLiteral("Could not read %1.").arg(path));
        return;
    }
    QString error;
    const QList<VocabularyEntry> imported = parseVocabularyCsv(file.readAll(), &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Vocabulary not imported"), error);
        return;
    }
    setRows(entries() + imported);
    updateLimit();
    emit changed();
}

void VocabularySettingsPage::updateLimit()
{
    if (m_updating) {
        return;
    }

    QStringList terms;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *item = m_table->item(row, 1);
        const QString term = item ? item->text().trimmed() : QString();
        if (!term.isEmpty()) {
            terms << term;
        }
    }
    const QList<VocabularyEntry> normalized = entries();
    if (normalized.size() != terms.size()) {
        setRows(normalized);
    }

    QStringList limitedTerms;
    for (const VocabularyEntry &entry : normalized) {
        limitedTerms.append(entry.term);
    }
    m_limit->setText(VocabularyLimit::summary(limitedTerms));
}

} // namespace speecher
