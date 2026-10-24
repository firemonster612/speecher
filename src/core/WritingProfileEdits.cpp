#include "core/WritingProfileEdits.h"

#include <QUuid>

#include <algorithm>

namespace speecher {

namespace {

// Long enough that two ids made a moment apart never meet.
constexpr int randomIdLength = 8;

// Removes entry id from `entries` and points every profile field that chose
// it at `instead`. The edit it returns puts the entry back at its place and
// points back the fields that still hold `instead`.
template <typename Choice>
SettingsEdit removeChoice(QList<Choice> RefinementSettings::*entries,
                          QString WritingProfileSettings::*field,
                          AppSettings &settings,
                          const QString &id,
                          const QString &instead)
{
    QList<Choice> &list = settings.refinement.*entries;
    const auto found = std::find_if(list.cbegin(), list.cend(), [&id](const Choice &choice) { return choice.id == id; });
    if (found == list.cend()) {
        return {};
    }
    const qsizetype index = found - list.cbegin();
    const Choice removed = list.takeAt(index);
    QStringList moved;
    for (WritingProfileSettings &profile : settings.refinement.writingProfiles) {
        if (profile.*field == id) {
            profile.*field = instead;
            moved.append(profile.profile);
        }
    }
    return [=](AppSettings &draft) {
        QList<Choice> &back = draft.refinement.*entries;
        back.insert(std::min(index, back.size()), removed);
        for (WritingProfileSettings &profile : draft.refinement.writingProfiles) {
            if (moved.contains(profile.profile) && profile.*field == instead) {
                profile.*field = id;
            }
        }
    };
}

} // namespace

QString newCustomId(const QString &prefix, const QStringList &taken)
{
    QString id;
    do {
        id = prefix + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::Id128).left(randomIdLength);
    } while (taken.contains(id));
    return id;
}

SettingsEdit removeCustomTone(AppSettings &settings, const QString &id)
{
    return removeChoice(&RefinementSettings::customTones, &WritingProfileSettings::tone, settings, id,
                        QStringLiteral("none"));
}

SettingsEdit removeCustomCleanupLevel(AppSettings &settings, const QString &id)
{
    const QList<CustomCleanupLevel> &levels = settings.refinement.customCleanupLevels;
    const auto level = std::find_if(levels.cbegin(), levels.cend(),
                                    [&id](const CustomCleanupLevel &entry) { return entry.id == id; });
    const QString base = level == levels.cend() || level->base == kCustomOnlyCleanupBase
        ? QStringLiteral("balanced")
        : level->base;
    return removeChoice(&RefinementSettings::customCleanupLevels, &WritingProfileSettings::cleanupStrength,
                        settings, id, base);
}

} // namespace speecher
