#include "frontend/qt/WritingProfileGrid.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace speecher {

namespace {

void addCleanupStrengths(QComboBox *combo)
{
    combo->addItem(QStringLiteral("None"), QStringLiteral("none"));
    combo->addItem(QStringLiteral("Light"), QStringLiteral("light_cleanup"));
    combo->addItem(QStringLiteral("Medium"), QStringLiteral("balanced"));
    combo->addItem(QStringLiteral("High"), QStringLiteral("strong_polish"));
}

void addWritingTones(QComboBox *combo)
{
    combo->addItem(QStringLiteral("No tone override"), QStringLiteral("none"));
    combo->addItem(QStringLiteral("Formal"), QStringLiteral("formal"));
    combo->addItem(QStringLiteral("Casual"), QStringLiteral("casual"));
    combo->addItem(QStringLiteral("Very casual"), QStringLiteral("very_casual"));
    combo->addItem(QStringLiteral("Excited"), QStringLiteral("excited"));
    combo->addItem(QStringLiteral("Gen Z"), QStringLiteral("gen_z"));
}

QList<WritingProfileSettings> gridSettings(const QTableWidget *grid)
{
    QList<WritingProfileSettings> settings;
    for (int row = 0; row < grid->rowCount(); ++row) {
        const QTableWidgetItem *profile = grid->item(row, 0);
        const auto *strength = qobject_cast<QComboBox *>(grid->cellWidget(row, 1));
        const auto *tone = qobject_cast<QComboBox *>(grid->cellWidget(row, 2));
        if (!profile || !strength || !tone) {
            continue;
        }
        settings.append({
            writingProfileFromName(profile->data(Qt::UserRole).toString()),
            strength->currentData().toString(),
            tone->currentData().toString(),
        });
    }
    return settings;
}

void setGridSettings(QTableWidget *grid,
                     const QList<WritingProfileSettings> &settings,
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
        addCleanupStrengths(strength);
        settings::selectData(strength, profileSettings.cleanupStrength);
        auto *tone = new QComboBox(grid);
        addWritingTones(tone);
        settings::selectData(tone, profileSettings.tone);
        QObject::connect(strength, &QComboBox::currentIndexChanged, grid, notifyChanged);
        QObject::connect(tone, &QComboBox::currentIndexChanged, grid, notifyChanged);
        grid->setItem(row, 0, profile);
        grid->setCellWidget(row, 1, strength);
        grid->setCellWidget(row, 2, tone);
    }
}

} // namespace

SchemaCustomRow makeWritingProfileGrid(QWidget *parent, std::function<void()> notifyChanged)
{
    auto *grid = new QTableWidget(parent);
    grid->setObjectName(QStringLiteral("vocabInput"));
    grid->setColumnCount(3);
    grid->setHorizontalHeaderLabels({
        QStringLiteral("Profile"),
        QStringLiteral("Cleanup"),
        QStringLiteral("Tone"),
    });
    grid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    grid->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    grid->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    grid->verticalHeader()->hide();
    grid->setSelectionMode(QAbstractItemView::NoSelection);
    grid->setMinimumHeight(207);
    grid->setMaximumHeight(207);

    return {
        grid,
        [grid] { return QVariant::fromValue(gridSettings(grid)); },
        [grid, notifyChanged = std::move(notifyChanged)](const QVariant &value) {
            setGridSettings(grid, value.value<QList<WritingProfileSettings>>(), notifyChanged);
        },
    };
}

} // namespace speecher
