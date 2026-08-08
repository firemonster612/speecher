#include "ui/settings/CorrectionsSettingsPage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtMath>

namespace speecher {

CorrectionsSettingsPage::CorrectionsSettingsPage(QWidget *parent)
    : QFrame(parent)
    , m_learnCorrections(new QCheckBox(this))
    , m_corrections(new QTableWidget(this))
    , m_removeCorrectionButton(new QPushButton(QStringLiteral("Delete selected"), this))
    , m_undoCorrectionButton(new QPushButton(QStringLiteral("Undo delete"), this))
    , m_undoLatestLearnButton(new QPushButton(QStringLiteral("Undo latest learn"), this))
{
    setObjectName(QStringLiteral("vocabSection"));
    m_learnCorrections->setText(QStringLiteral("Learn corrections"));
    m_learnCorrections->setToolTip(QStringLiteral("Observe a verified inserted span briefly and save only high-confidence corrections."));
    m_corrections->setObjectName(QStringLiteral("vocabInput"));
    m_corrections->setColumnCount(4);
    m_corrections->setHorizontalHeaderLabels({
        QStringLiteral("Enabled"),
        QStringLiteral("Heard"),
        QStringLiteral("Corrected"),
        QStringLiteral("App"),
    });
    m_corrections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_corrections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_corrections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_corrections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_corrections->verticalHeader()->hide();
    m_corrections->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_corrections->setSelectionMode(QAbstractItemView::SingleSelection);
    m_corrections->setMinimumHeight(180);
    m_undoCorrectionButton->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        QStringLiteral("Source-marked corrections learned after verified insertion. Edit, disable, delete, or undo deletions here."),
        this);
    description->setObjectName(QStringLiteral("rowDescription"));
    description->setWordWrap(true);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_learnCorrections);
    buttons->addStretch();
    buttons->addWidget(m_undoLatestLearnButton);
    buttons->addWidget(m_undoCorrectionButton);
    buttons->addWidget(m_removeCorrectionButton);
    layout->addWidget(description);
    layout->addWidget(m_corrections);
    layout->addLayout(buttons);

    connect(m_learnCorrections, &QCheckBox::toggled, this, &CorrectionsSettingsPage::changed);
    connect(m_corrections, &QTableWidget::itemChanged, this, &CorrectionsSettingsPage::changed);
    connect(m_corrections, &QTableWidget::itemSelectionChanged, this, [this] {
        m_removeCorrectionButton->setEnabled(m_corrections->currentRow() >= 0);
    });
    connect(m_removeCorrectionButton, &QPushButton::clicked, this, [this] {
        const int row = m_corrections->currentRow();
        QList<LearnedCorrection> current = corrections();
        if (row < 0 || row >= current.size()) {
            return;
        }
        m_removedCorrections.append(current.takeAt(row));
        setCorrections(current);
        m_undoCorrectionButton->setEnabled(true);
        emit changed();
    });
    connect(m_undoCorrectionButton, &QPushButton::clicked, this, [this] {
        if (m_removedCorrections.isEmpty()) {
            return;
        }
        QList<LearnedCorrection> current = corrections();
        current.prepend(m_removedCorrections.takeLast());
        setCorrections(current);
        m_undoCorrectionButton->setEnabled(!m_removedCorrections.isEmpty());
        emit changed();
    });
    connect(m_undoLatestLearnButton, &QPushButton::clicked, this, [this] {
        QList<LearnedCorrection> current = corrections();
        if (current.isEmpty()) {
            return;
        }
        m_removedCorrections.append(current.takeFirst());
        setCorrections(current);
        m_undoCorrectionButton->setEnabled(true);
        emit changed();
    });
}

void CorrectionsSettingsPage::load(bool learningEnabled, const QList<LearnedCorrection> &corrections)
{
    m_learnCorrections->setChecked(learningEnabled);
    m_removedCorrections.clear();
    m_undoCorrectionButton->setEnabled(false);
    setCorrections(corrections);
    m_undoLatestLearnButton->setEnabled(!corrections.isEmpty());
}

bool CorrectionsSettingsPage::learningEnabled() const
{
    return m_learnCorrections->isChecked();
}

QList<LearnedCorrection> CorrectionsSettingsPage::corrections() const
{
    QList<LearnedCorrection> result;
    for (int row = 0; row < m_corrections->rowCount(); ++row) {
        const QTableWidgetItem *enabled = m_corrections->item(row, 0);
        const QTableWidgetItem *original = m_corrections->item(row, 1);
        const QTableWidgetItem *corrected = m_corrections->item(row, 2);
        const QTableWidgetItem *application = m_corrections->item(row, 3);
        if (!enabled || !original || !corrected || !application) {
            continue;
        }
        LearnedCorrection correction;
        correction.id = enabled->data(Qt::UserRole).toString();
        correction.createdAtMs = enabled->data(Qt::UserRole + 1).toLongLong();
        correction.confidence = enabled->data(Qt::UserRole + 2).toDouble();
        correction.enabled = enabled->checkState() == Qt::Checked;
        correction.original = original->text().trimmed();
        correction.corrected = corrected->text().trimmed();
        correction.applicationId = application->text().trimmed();
        if (!correction.id.isEmpty() && !correction.original.isEmpty() && !correction.corrected.isEmpty()) {
            result.append(correction);
        }
    }
    return result;
}

bool CorrectionsSettingsPage::hasChanges(bool learningEnabled, const QList<LearnedCorrection> &corrections) const
{
    return this->learningEnabled() != learningEnabled || this->corrections() != corrections;
}

void CorrectionsSettingsPage::setCorrections(const QList<LearnedCorrection> &corrections)
{
    QSignalBlocker blocker(m_corrections);
    m_corrections->setRowCount(0);
    for (const LearnedCorrection &correction : corrections) {
        const int row = m_corrections->rowCount();
        m_corrections->insertRow(row);
        auto *enabled = new QTableWidgetItem;
        enabled->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        enabled->setCheckState(correction.enabled ? Qt::Checked : Qt::Unchecked);
        enabled->setData(Qt::UserRole, correction.id);
        enabled->setData(Qt::UserRole + 1, correction.createdAtMs);
        enabled->setData(Qt::UserRole + 2, correction.confidence);
        auto *original = new QTableWidgetItem(correction.original);
        auto *corrected = new QTableWidgetItem(correction.corrected);
        auto *application = new QTableWidgetItem(correction.applicationId);
        application->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        application->setToolTip(QStringLiteral("Learned automatically · confidence %1%")
                                    .arg(qRound(correction.confidence * 100.0)));
        m_corrections->setItem(row, 0, enabled);
        m_corrections->setItem(row, 1, original);
        m_corrections->setItem(row, 2, corrected);
        m_corrections->setItem(row, 3, application);
    }
    m_removeCorrectionButton->setEnabled(false);
    if (m_undoLatestLearnButton) {
        m_undoLatestLearnButton->setEnabled(!corrections.isEmpty());
    }
}

} // namespace speecher
