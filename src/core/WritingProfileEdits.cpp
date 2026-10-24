#include "core/WritingProfileEdits.h"

#include "core/ProviderChain.h"

#include <QUuid>

#include <algorithm>

namespace speecher {

namespace {

// Long enough that two ids made a moment apart never meet.
constexpr int randomIdLength = 8;

const QStringList cleanupBases{QStringLiteral("light_cleanup"), QStringLiteral("balanced"),
                               QStringLiteral("strong_polish"), kCustomOnlyCleanupBase};

// The index of the entry of list whose idOf is id, or -1.
template <typename T, typename IdOf>
qsizetype indexOf(const QList<T> &list, IdOf idOf, const QString &id)
{
    const auto found = std::find_if(list.cbegin(), list.cend(), [&](const T &entry) { return idOf(entry) == id; });
    return found == list.cend() ? -1 : found - list.cbegin();
}

// Whether no entry of list but the one at index `self` is called `name`,
// ignoring case: two entries by one name would read as one.
template <typename T, typename NameOf>
bool nameFree(const QList<T> &list, qsizetype self, const QString &name, NameOf nameOf)
{
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (i != self && nameOf(list.at(i)).compare(name, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return true;
}

// A new entry's id: one of the user's, and not one list holds.
template <typename T, typename IdOf>
bool idFree(const QList<T> &list, IdOf idOf, const QString &id)
{
    return id.startsWith(kCustomIdPrefix) && indexOf(list, idOf, id) < 0;
}

bool levelOffered(const QString &id, const RefinementSettings &refinement)
{
    return kBuiltInCleanupLevels.contains(id)
        || indexOf(refinement.customCleanupLevels, [](const CustomCleanupLevel &level) { return level.id; }, id) >= 0;
}

bool toneOffered(const QString &id, const RefinementSettings &refinement)
{
    return kBuiltInTones.contains(id)
        || indexOf(refinement.customTones, [](const CustomTone &tone) { return tone.id; }, id) >= 0;
}

// An empty service is the one the Speech to text or Refinement page chose.
bool serviceOffered(ProviderRole role, const QString &id)
{
    return id.isEmpty() || isChainProviderId(role, id);
}

QString profileId(const WritingProfileSettings &profile)
{
    return profile.profile;
}

QString toneId(const CustomTone &tone)
{
    return tone.id;
}

QString levelId(const CustomCleanupLevel &level)
{
    return level.id;
}

// Tidies the profile at `self` of the settings' profiles (-1 for one being
// added) and says what it can't hold. `stored` is what it held before, and
// each choice it held already stays valid, even one no longer offered.
EditRefusal checkProfile(WritingProfileSettings &profile, const WritingProfileSettings *stored, qsizetype self,
                         const RefinementSettings &refinement)
{
    profile.outputLanguage = profile.outputLanguage.trimmed();
    profile.speechModel = profile.speechModel.trimmed();
    profile.refinementModel = profile.refinementModel.trimmed();
    if (isBuiltInWritingProfile(profile.profile)) {
        if (stored && profile.name != stored->name) {
            return EditRefusal::BuiltIn;
        }
    } else {
        profile.name = profile.name.trimmed();
        const QList<WritingProfileSettings> &profiles = refinement.writingProfiles;
        if (profile.name.isEmpty()) {
            return EditRefusal::Blank;
        }
        if (!nameFree(profiles, self, profile.name, [&profiles](const WritingProfileSettings &other) {
                return writingProfileLabel(other.profile, profiles);
            })) {
            return EditRefusal::Taken;
        }
    }
    const auto kept = [stored](QString WritingProfileSettings::*field, const QString &value) {
        return stored && stored->*field == value;
    };
    const bool offered =
        (kept(&WritingProfileSettings::cleanupStrength, profile.cleanupStrength)
         || levelOffered(profile.cleanupStrength, refinement))
        && (kept(&WritingProfileSettings::tone, profile.tone) || toneOffered(profile.tone, refinement))
        && (kept(&WritingProfileSettings::speechProvider, profile.speechProvider)
            || serviceOffered(ProviderRole::Speech, profile.speechProvider))
        && (kept(&WritingProfileSettings::refinementProvider, profile.refinementProvider)
            || serviceOffered(ProviderRole::Refinement, profile.refinementProvider));
    return offered ? EditRefusal::None : EditRefusal::Missing;
}

EditRefusal checkTone(CustomTone &tone, qsizetype self, const QList<CustomTone> &tones)
{
    tone.name = tone.name.trimmed();
    if (tone.name.isEmpty()) {
        return EditRefusal::Blank;
    }
    if (!nameFree(tones, self, tone.name, [](const CustomTone &other) { return other.name; })) {
        return EditRefusal::Taken;
    }
    return tone.instruction.trimmed().isEmpty() ? EditRefusal::Incomplete : EditRefusal::None;
}

EditRefusal checkLevel(CustomCleanupLevel &level, qsizetype self, const QList<CustomCleanupLevel> &levels)
{
    level.name = level.name.trimmed();
    if (level.name.isEmpty()) {
        return EditRefusal::Blank;
    }
    if (!nameFree(levels, self, level.name, [](const CustomCleanupLevel &other) { return other.name; })) {
        return EditRefusal::Taken;
    }
    if (!cleanupBases.contains(level.base)) {
        return EditRefusal::Missing;
    }
    // Custom only is nothing but the instructions.
    const bool instructed = level.base != kCustomOnlyCleanupBase || !level.instructions.trimmed().isEmpty();
    return instructed ? EditRefusal::None : EditRefusal::Incomplete;
}

QString ruleMatch(const AppRecognitionRule &rule)
{
    return rule.match;
}

bool sameMatch(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

// The app matches with the overrides they replaced folded in and the
// overrides cleared, so saving the draft retires them.
QList<AppRecognitionRule> &rulesWithOverridesRetired(AppSettings &settings)
{
    settings.appRecognitionRules = recognitionRulesWithMigratedProfileOverrides(
        settings.appRecognitionRules, settings.refinement.writingProfileOverrides);
    settings.refinement.writingProfileOverrides.clear();
    return settings.appRecognitionRules;
}

qsizetype indexOfMatch(const QList<AppRecognitionRule> &rules, const QString &match)
{
    const auto found = std::find_if(rules.cbegin(), rules.cend(), [&match](const AppRecognitionRule &rule) {
        return sameMatch(rule.match, match.trimmed());
    });
    return found == rules.cend() ? -1 : found - rules.cbegin();
}

EditRefusal checkRule(AppRecognitionRule &rule, qsizetype self, const AppSettings &settings)
{
    rule.match = rule.match.trimmed();
    if (rule.match.isEmpty()) {
        return EditRefusal::Blank;
    }
    if (!nameFree(settings.appRecognitionRules, self, rule.match, ruleMatch)) {
        return EditRefusal::Taken;
    }
    if (!rule.category && !rule.writingProfile) {
        return EditRefusal::Incomplete;
    }
    const bool profileOffered = !rule.writingProfile
        || !offeredWritingProfile(*rule.writingProfile, settings.refinement.writingProfiles, {}).isEmpty();
    return profileOffered ? EditRefusal::None : EditRefusal::Missing;
}

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
    const qsizetype index = indexOf(list, [](const Choice &choice) { return choice.id; }, id);
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(list, index, [](const Choice &entry, const Choice &removed) {
        return entry.id == removed.id;
    });
    QStringList moved;
    for (WritingProfileSettings &profile : settings.refinement.writingProfiles) {
        if (profile.*field == id) {
            profile.*field = instead;
            moved.append(profile.profile);
        }
    }
    return [=](AppSettings &draft) {
        putBack(draft.refinement.*entries);
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

EditRefusal addWritingProfile(AppSettings &settings, WritingProfileSettings profile)
{
    QList<WritingProfileSettings> &profiles = settings.refinement.writingProfiles;
    if (!idFree(profiles, profileId, profile.profile)) {
        return EditRefusal::Taken;
    }
    return appendEntry(profiles, profile, [&settings](WritingProfileSettings &added, qsizetype self) {
        return checkProfile(added, nullptr, self, settings.refinement);
    });
}

EditRefusal editWritingProfile(AppSettings &settings, const QString &id,
                               const EntryChange<WritingProfileSettings> &change)
{
    QList<WritingProfileSettings> &profiles = settings.refinement.writingProfiles;
    return changeEntry(profiles, indexOf(profiles, profileId, id), change,
                       [&settings, &profiles](WritingProfileSettings &profile, qsizetype self) {
                           const WritingProfileSettings stored = profiles.at(self);
                           return checkProfile(profile, &stored, self, settings.refinement);
                       });
}

bool removeWritingProfile(AppSettings &settings, const QString &id)
{
    return !isBuiltInWritingProfile(id)
        && settings.refinement.writingProfiles.removeIf(
               [&id](const WritingProfileSettings &profile) { return profile.profile == id; })
        > 0;
}

EditRefusal addCustomTone(AppSettings &settings, CustomTone tone)
{
    QList<CustomTone> &tones = settings.refinement.customTones;
    if (!idFree(tones, toneId, tone.id)) {
        return EditRefusal::Taken;
    }
    return appendEntry(tones, tone, [&tones](CustomTone &added, qsizetype self) { return checkTone(added, self, tones); });
}

EditRefusal editCustomTone(AppSettings &settings, const QString &id, const EntryChange<CustomTone> &change)
{
    QList<CustomTone> &tones = settings.refinement.customTones;
    return changeEntry(tones, indexOf(tones, toneId, id), change,
                       [&tones](CustomTone &tone, qsizetype self) { return checkTone(tone, self, tones); });
}

EditRefusal addCustomCleanupLevel(AppSettings &settings, CustomCleanupLevel level)
{
    QList<CustomCleanupLevel> &levels = settings.refinement.customCleanupLevels;
    if (!idFree(levels, levelId, level.id)) {
        return EditRefusal::Taken;
    }
    return appendEntry(levels, level,
                       [&levels](CustomCleanupLevel &added, qsizetype self) { return checkLevel(added, self, levels); });
}

EditRefusal editCustomCleanupLevel(AppSettings &settings, const QString &id,
                                   const EntryChange<CustomCleanupLevel> &change)
{
    QList<CustomCleanupLevel> &levels = settings.refinement.customCleanupLevels;
    return changeEntry(levels, indexOf(levels, levelId, id), change,
                       [&levels](CustomCleanupLevel &level, qsizetype self) { return checkLevel(level, self, levels); });
}

SettingsEdit removeCustomTone(AppSettings &settings, const QString &id)
{
    return removeChoice(&RefinementSettings::customTones, &WritingProfileSettings::tone, settings, id,
                        QStringLiteral("none"));
}

SettingsEdit removeCustomCleanupLevel(AppSettings &settings, const QString &id)
{
    const QList<CustomCleanupLevel> &levels = settings.refinement.customCleanupLevels;
    const qsizetype index = indexOf(levels, levelId, id);
    const QString base = index < 0 || levels.at(index).base == kCustomOnlyCleanupBase ? QStringLiteral("balanced")
                                                                                       : levels.at(index).base;
    return removeChoice(&RefinementSettings::customCleanupLevels, &WritingProfileSettings::cleanupStrength,
                        settings, id, base);
}

EditRefusal addAppRecognitionRule(AppSettings &settings, AppRecognitionRule rule)
{
    return appendEntry(rulesWithOverridesRetired(settings), rule, [&settings](AppRecognitionRule &added, qsizetype self) {
        return checkRule(added, self, settings);
    });
}

EditRefusal editAppRecognitionRule(AppSettings &settings, const QString &match,
                                   const EntryChange<AppRecognitionRule> &change)
{
    QList<AppRecognitionRule> &rules = rulesWithOverridesRetired(settings);
    return changeEntry(rules, indexOfMatch(rules, match), change,
                       [&settings](AppRecognitionRule &rule, qsizetype self) { return checkRule(rule, self, settings); });
}

SettingsEdit removeAppRecognitionRule(AppSettings &settings, const QString &match)
{
    QList<AppRecognitionRule> &rules = rulesWithOverridesRetired(settings);
    const qsizetype index = indexOfMatch(rules, match);
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(rules, index, [](const AppRecognitionRule &entry, const AppRecognitionRule &removed) {
        return sameMatch(entry.match, removed.match);
    });
    return [putBack](AppSettings &draft) { putBack(rulesWithOverridesRetired(draft)); };
}

} // namespace speecher
