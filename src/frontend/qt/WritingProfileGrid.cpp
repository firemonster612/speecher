#include "frontend/qt/WritingProfileGrid.h"

#include "frontend/qt/CollectionRow.h"
#include "ui/settings/SettingsPageSupport.h"
#include "core/settings/SettingsSchema.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>

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
        settings.append({
            writingProfileFromName(profile->data(Qt::UserRole).toString()),
            strength->currentData().toString(),
            tone->currentData().toString(),
            instructions->text(),
        });
    }
    return settings;
}

// The custom levels and tones each profile's pickers offer after the built-ins.
struct GridChoices {
    QList<CustomCleanupLevel> levels;
    QList<CustomTone> tones;
};

void setGridSettings(QTableWidget *grid,
                     const QList<WritingProfileSettings> &settings,
                     const GridChoices &choices,
                     const std::function<void()> &notifyChanged)
{
    const QSignalBlocker blocker(grid);
    grid->setRowCount(0);
    for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
        const WritingProfileSettings profileSettings =
            writingProfileSettingsFor(settings, fallback.profile);
        const int row = grid->rowCount();
        grid->insertRow(row);
        auto *profile = new QTableWidgetItem(writingProfileLabel(fallback.profile));
        profile->setFlags(Qt::ItemIsEnabled);
        profile->setData(Qt::UserRole, writingProfileName(fallback.profile));
        auto *strength = new QComboBox(grid);
        for (const RowOption &option : cleanupStrengths(choices.levels)) strength->addItem(option.label, option.id);
        // A level or tone deleted from the draft shows as what saving makes it.
        settings::selectData(strength, offeredCleanupLevel(profileSettings.cleanupStrength, choices.levels));
        auto *tone = new QComboBox(grid);
        for (const RowOption &option : writingTones(choices.tones)) tone->addItem(option.label, option.id);
        settings::selectData(tone, offeredTone(profileSettings.tone, choices.tones));
        QObject::connect(strength, &QComboBox::currentIndexChanged, grid, notifyChanged);
        QObject::connect(tone, &QComboBox::currentIndexChanged, grid, notifyChanged);
        grid->setItem(row, 0, profile);
        grid->setCellWidget(row, 1, strength);
        grid->setCellWidget(row, 2, tone);
        grid->setItem(row, 3, new QTableWidgetItem(profileSettings.instructions));
    }
}

} // namespace

SchemaCustomRow makeWritingProfileGrid(QWidget *parent, std::function<void()> notifyChanged)
{
    auto *grid = new QTableWidget(parent);
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
    grid->setMinimumHeight(207);
    grid->setMaximumHeight(207);

    QObject::connect(grid, &QTableWidget::itemChanged, grid, notifyChanged);

    // The page hands over its settings before each value, so the pickers
    // offer the tones and levels those settings hold.
    auto choices = std::make_shared<GridChoices>();
    return {
        grid,
        [grid] { return QVariant::fromValue(gridSettings(grid)); },
        [grid, choices, notifyChanged = std::move(notifyChanged)](const QVariant &value) {
            setGridSettings(grid, value.value<QList<WritingProfileSettings>>(), *choices, notifyChanged);
        },
        true,
        nullptr,
        [choices](const AppSettings &settings) {
            *choices = {settings.refinement.customCleanupLevels, settings.refinement.customTones};
        },
    };
}

} // namespace speecher
