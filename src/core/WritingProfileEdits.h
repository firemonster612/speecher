#pragma once

#include "core/SettingsEdit.h"

#include <QString>
#include <QStringList>

// The edits the user makes to a draft of the settings' Writing Profiles and
// rules: adding and changing one, refused with the reason when the list would
// hold something it can't, and removing one together with what named it, with
// the edit that puts it back. (Saving a draft without a profile already drops
// what named the profile: see SettingsCodecs.) And the ids of what the user
// adds.

namespace speecher {

// An id for a profile, rule or App Type the user adds: prefix, which starts
// with kCustomIdPrefix, then a random part. Random rather than counted, so
// an id is never given twice and an undone deletion never finds its id
// taken by something added meanwhile. Never one of `taken`.
QString newCustomId(const QString &prefix, const QStringList &taken);

// Adds one of the user's profiles, after the others. Its id must be a
// newCustomId no profile has (Taken otherwise). It needs a name no other
// profile goes by, and cleanup level, tone and services that are offered.
EditRefusal addWritingProfile(AppSettings &settings, WritingProfileSettings profile);
// Changes the profile id as change says, with the same rules; a choice the
// profile held already stays valid. A built-in profile keeps its name. A new
// cleanup level clears the profile's switches for built-in rules.
EditRefusal editWritingProfile(AppSettings &settings, const QString &id,
                               const EntryChange<WritingProfileSettings> &change);
// Removes one of the user's profiles; whether there was one by that id.
bool removeWritingProfile(AppSettings &settings, const QString &id);

// Adds one of the user's rules, after the others: a fresh custom id, and a
// name no other of the user's rules goes by.
EditRefusal addRule(AppSettings &settings, RefinementRule rule);
EditRefusal editRule(AppSettings &settings, const QString &id, const EntryChange<RefinementRule> &change);
// Removes the rule and every profile's switch for it. Returns the edit that
// puts both back, the switches only in profiles that have none for it by
// then; it does nothing once a rule by that name was added since. Empty for
// an id the settings do not hold.
SettingsEdit removeRule(AppSettings &settings, const QString &id);
// Sets a profile's switch for a built-in rule or one of the user's, dropping
// it when it equals the rule's default (ruleDefault). Missing for a profile
// or rule the settings do not hold.
EditRefusal setProfileRule(AppSettings &settings, const QString &profile, const QString &rule, bool on);

} // namespace speecher
