#include "frontend/qt/WritingProfileGrid.h"

#include "frontend/qt/CollectionRow.h"
#include "ui/settings/SettingsPageSupport.h"
#include "core/settings/SettingsSchema.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <memory>

namespace speecher {

namespace {

QList<WritingProfileSettings> gridSettings(const QTableWidget *grid)
{
    QList<WritingProfileSettings> settings;
    for (int row = 0; row < grid->rowCount(); ++row) {
        const QTableWidgetItem *profile = grid->item(row, 0);
        const auto *strength = qobject_cast<QComboBox *>(grid->cellWidget(row, 1));
        const auto *tone = qobject_cast<QComboBox *>(grid->cellWidget(row, 2));
        const QTableWidgetItem *instructions = grid->item(row, 3);
        if (!profile || !strength || !tone || !instructions) {
            continue;
        }
        const QString id = profile->data(Qt::UserRole).toString();
        settings.append({
            id,
            strength->currentData().toString(),
            tone->currentData().toString(),
            instructions->text(),
            isBuiltInWritingProfile(id) ? QString() : profile->text(),
        });
    }
    return settings;
}

// The draft the page last handed over: the custom levels and tones each
// profile's pickers offer after the built-ins, and the rules and fallback a
// delete would change.
using GridDraft = std::shared_ptr<AppSettings>;

void appendProfileRow(QTableWidget *grid,
                      const WritingProfileSettings &profileSettings,
                      const QString &label,
                      const AppSettings &draft,
                      const std::function<void()> &notifyChanged)
{
    const QList<CustomCleanupLevel> &levels = draft.refinement.customCleanupLevels;
    const QList<CustomTone> &tones = draft.refinement.customTones;
    const int row = grid->rowCount();
    grid->insertRow(row);
    auto *profile = new QTableWidgetItem(label);
    // A custom profile's name can be edited; a built-in's cannot.
    profile->setFlags(isBuiltInWritingProfile(profileSettings.profile)
                          ? Qt::ItemIsEnabled
                          : Qt::ItemIsEnabled | Qt::ItemIsEditable);
    profile->setData(Qt::UserRole, profileSettings.profile);
    auto *strength = new QComboBox(grid);
    for (const RowOption &option : cleanupStrengths(levels)) strength->addItem(option.label, option.id);
    // A level or tone deleted from the draft shows as what saving makes it.
    settings::selectData(strength, offeredCleanupLevel(profileSettings.cleanupStrength, levels));
    auto *tone = new QComboBox(grid);
    for (const RowOption &option : writingTones(tones)) tone->addItem(option.label, option.id);
    settings::selectData(tone, offeredTone(profileSettings.tone, tones));
    QObject::connect(strength, &QComboBox::currentIndexChanged, grid, notifyChanged);
    QObject::connect(tone, &QComboBox::currentIndexChanged, grid, notifyChanged);
    grid->setItem(row, 0, profile);
    grid->setCellWidget(row, 1, strength);
    grid->setCellWidget(row, 2, tone);
    grid->setItem(row, 3, new QTableWidgetItem(profileSettings.instructions));
}

// Tall enough for every profile, so adding one grows the grid rather than
// scrolling it.
void fitToRows(QTableWidget *grid)
{
    int height = grid->horizontalHeader()->sizeHint().height() + 2 * grid->frameWidth();
    for (int row = 0; row < grid->rowCount(); ++row) {
        height += grid->rowHeight(row);
    }
    grid->setFixedHeight(height);
}

void setGridSettings(QTableWidget *grid,
                     const QList<WritingProfileSettings> &settings,
                     const AppSettings &draft,
                     const std::function<void()> &notifyChanged)
{
    const QSignalBlocker blocker(grid);
    grid->setRowCount(0);
    for (const RowOption &profile : writingProfileChoices(settings)) {
        appendProfileRow(grid, writingProfileSettingsFor(settings, profile.id), profile.label, draft,
                         notifyChanged);
    }
    grid->setCurrentCell(-1, -1);
    fitToRows(grid);
}

// The row Delete acts on: the current one, when it is a custom profile.
int currentCustomRow(const QTableWidget *grid)
{
    const QTableWidgetItem *profile = grid->item(grid->currentRow(), 0);
    return profile && !isBuiltInWritingProfile(profile->data(Qt::UserRole).toString()) ? grid->currentRow()
                                                                                       : -1;
}

} // namespace

SchemaCustomRow makeWritingProfileGrid(QWidget *parent, std::function<void()> notifyChanged)
{
    auto *block = new QWidget(parent);
    auto *grid = new QTableWidget(block);
    grid->setObjectName(QStringLiteral("vocabInput"));
    grid->setColumnCount(4);
    grid->setHorizontalHeaderLabels({
        QStringLiteral("Profile"),
        QStringLiteral("Cleanup"),
        QStringLiteral("Tone"),
        QStringLiteral("Instructions"),
    });
    grid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    grid->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    grid->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    grid->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    useMultilineEditor(grid, 3);
    grid->verticalHeader()->hide();
    grid->setSelectionMode(QAbstractItemView::NoSelection);

    auto *remove = new QPushButton(QStringLiteral("Delete"), block);
    remove->setObjectName(QStringLiteral("deleteWritingProfile"));
    remove->setEnabled(false);
    auto *add = new QPushButton(QStringLiteral("Add profile"), block);
    add->setObjectName(QStringLiteral("addWritingProfile"));
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(remove);
    buttons->addWidget(add);
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(grid);
    layout->addLayout(buttons);

    QObject::connect(grid, &QTableWidget::itemChanged, grid, notifyChanged);
    QObject::connect(grid, &QTableWidget::currentCellChanged, remove, [grid, remove] {
        remove->setEnabled(currentCustomRow(grid) >= 0);
    });

    // The page hands over its settings before each value, so the pickers
    // offer the tones and levels those settings hold.
    auto draft = std::make_shared<AppSettings>();
    // Quiet until the first edit, as in a collection editor, so the new
    // profile's id comes from the name it is given.
    QObject::connect(add, &QPushButton::clicked, grid, [grid, draft, notifyChanged] {
        {
            const QSignalBlocker blocker(grid);
            appendProfileRow(grid, {QString()}, QStringLiteral("New profile"), *draft, notifyChanged);
            fitToRows(grid);
        }
        const int row = grid->rowCount() - 1;
        grid->setCurrentCell(row, 0);
        grid->editItem(grid->item(row, 0));
    });
    QObject::connect(remove, &QPushButton::clicked, grid, [grid, draft, notifyChanged] {
        const int row = currentCustomRow(grid);
        if (row < 0) {
            return;
        }
        const QString notice =
            writingProfileDeletionNotice(*draft, grid->item(row, 0)->data(Qt::UserRole).toString());
        if (!notice.isEmpty()
            && QMessageBox::question(grid, QStringLiteral("Delete profile"), notice,
                                     QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel)
                   != QMessageBox::Ok) {
            return;
        }
        {
            const QSignalBlocker blocker(grid);
            grid->removeRow(row);
            fitToRows(grid);
        }
        notifyChanged();
    });
    return {
        block,
        [grid] { return QVariant::fromValue(gridSettings(grid)); },
        [grid, remove, draft, notifyChanged = std::move(notifyChanged)](const QVariant &value) {
            setGridSettings(grid, value.value<QList<WritingProfileSettings>>(), *draft, notifyChanged);
            remove->setEnabled(false);
        },
        true,
        nullptr,
        [draft](const AppSettings &settings) { *draft = settings; },
    };
}

} // namespace speecher
