#include "core/WritingProfileEdits.h"

#include "core/ProviderChain.h"
#include "core/RefinementRules.h"

#include <QUuid>

#include <algorithm>

namespace speecher {

namespace {

// Long enough that two ids made a moment apart never meet.
constexpr int randomIdLength = 8;

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

// An empty service is the one the Speech to text or Refinement page chose.
bool serviceOffered(ProviderRole role, const std::optional<ServiceChoice> &first)
{
    return !first || isChainProviderId(role, first->service);
}

QString profileId(const WritingProfileSettings &profile)
{
    return profile.profile;
}

QString ruleId(const RefinementRule &rule)
{
    return rule.id;
}

// Tidies the profile at `self` of the settings' profiles (-1 for one being
// added) and says what it can't hold. `stored` is what it held before, and
// each service it held already stays valid, even one no longer offered.
EditRefusal checkProfile(WritingProfileSettings &profile, const WritingProfileSettings *stored, qsizetype self,
                         const RefinementSettings &refinement)
{
    profile.outputLanguage = profile.outputLanguage.trimmed();
    for (std::optional<ServiceChoice> *first : {&profile.speechFirst, &profile.refinementFirst}) {
        if (*first) {
            (*first)->model = (*first)->model.trimmed();
        }
    }
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
    const auto keptService = [stored, &profile](std::optional<ServiceChoice> WritingProfileSettings::*field) {
        const auto service = [](const std::optional<ServiceChoice> &first) { return first ? first->service : QString(); };
        return stored && service(stored->*field) == service(profile.*field);
    };
    const bool offered =
        kBuiltInCleanupLevels.contains(profile.cleanupStrength) && kBuiltInTones.contains(profile.tone)
        && (keptService(&WritingProfileSettings::speechFirst)
            || serviceOffered(ProviderRole::Speech, profile.speechFirst))
        && (keptService(&WritingProfileSettings::refinementFirst)
            || serviceOffered(ProviderRole::Refinement, profile.refinementFirst));
    return offered ? EditRefusal::None : EditRefusal::Missing;
}

EditRefusal checkRule(RefinementRule &rule, qsizetype self, const QList<RefinementRule> &rules)
{
    rule.name = rule.name.trimmed();
    if (rule.name.isEmpty()) {
        return EditRefusal::Blank;
    }
    return nameFree(rules, self, rule.name, [](const RefinementRule &other) { return other.name; })
        ? EditRefusal::None
        : EditRefusal::Taken;
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
                           if (profile.cleanupStrength != stored.cleanupStrength) {
                               resetBuiltInRules(profile);
                           }
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

EditRefusal addRule(AppSettings &settings, RefinementRule rule)
{
    QList<RefinementRule> &rules = settings.refinement.rules;
    if (!idFree(rules, ruleId, rule.id)) {
        return EditRefusal::Taken;
    }
    return appendEntry(rules, rule, [&rules](RefinementRule &added, qsizetype self) {
        return checkRule(added, self, rules);
    });
}

EditRefusal editRule(AppSettings &settings, const QString &id, const EntryChange<RefinementRule> &change)
{
    QList<RefinementRule> &rules = settings.refinement.rules;
    return changeEntry(rules, indexOf(rules, ruleId, id), change,
                       [&rules](RefinementRule &rule, qsizetype self) { return checkRule(rule, self, rules); });
}

SettingsEdit removeRule(AppSettings &settings, const QString &id)
{
    QList<RefinementRule> &rules = settings.refinement.rules;
    const qsizetype index = indexOf(rules, ruleId, id);
    if (index < 0) {
        return {};
    }
    const auto putBack = takeEntry(rules, index, [](const RefinementRule &entry, const RefinementRule &removed) {
        return entry.id == removed.id || entry.name.compare(removed.name, Qt::CaseInsensitive) == 0;
    });
    QHash<QString, bool> switches;
    for (WritingProfileSettings &profile : settings.refinement.writingProfiles) {
        if (profile.rules.contains(id)) {
            switches.insert(profile.profile, profile.rules.take(id));
        }
    }
    return [=](AppSettings &draft) {
        QList<RefinementRule> &back = draft.refinement.rules;
        putBack(back);
        if (indexOf(back, ruleId, id) < 0) {
            return;
        }
        for (WritingProfileSettings &profile : draft.refinement.writingProfiles) {
            if (switches.contains(profile.profile) && !profile.rules.contains(id)) {
                profile.rules.insert(id, switches.value(profile.profile));
            }
        }
    };
}

EditRefusal setProfileRule(AppSettings &settings, const QString &profile, const QString &rule, bool on)
{
    QList<WritingProfileSettings> &profiles = settings.refinement.writingProfiles;
    const qsizetype index = indexOf(profiles, profileId, profile);
    const QList<RefinementRule> &rules = settings.refinement.rules;
    if (index < 0 || !ruleExists(rule, rules)) {
        return EditRefusal::Missing;
    }
    WritingProfileSettings &switched = profiles[index];
    if (on == ruleDefault(rule, switched, switched.cleanupStrength, rules)) {
        switched.rules.remove(rule);
    } else {
        switched.rules.insert(rule, on);
    }
    return EditRefusal::None;
}

} // namespace speecher
