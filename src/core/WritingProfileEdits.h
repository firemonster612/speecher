#pragma once

#include "core/SettingsEdit.h"

#include <QString>
#include <QStringList>

// The edits the user makes to a draft of the settings' Writing Profiles,
// custom tones and cleanup levels and app matches: adding and changing one,
// refused with the reason when the list would hold something it can't, and
// removing one together with what named it, with the edit that puts it back.
// (Saving a draft without a profile already drops what named the profile:
// see SettingsCodecs.) And the ids of what the user adds.

namespace speecher {

// An id for a profile, tone or cleanup level the user adds: prefix, which
// starts with kCustomIdPrefix, then a random part. Random rather than
// counted, so an id is never given twice and an undone deletion never finds
// its id taken by something added meanwhile. Never one of `taken`.
QString newCustomId(const QString &prefix, const QStringList &taken);

// Adds one of the user's profiles, after the others. Its id must be a
// newCustomId no profile has (Taken otherwise). It needs a name no other
// profile goes by, and cleanup level, tone and services that are offered.
EditRefusal addWritingProfile(AppSettings &settings, WritingProfileSettings profile);
// Changes the profile id as change says, with the same rules; a choice the
// profile held already stays valid. A built-in profile keeps its name.
EditRefusal editWritingProfile(AppSettings &settings, const QString &id,
                               const EntryChange<WritingProfileSettings> &change);
// Removes one of the user's profiles; whether there was one by that id.
bool removeWritingProfile(AppSettings &settings, const QString &id);

// Adds a custom tone, after the others: a fresh custom id, a name no other
// custom tone goes by, and an instruction. builtInNames are what the page
// calls the built-in tones, which a custom one can't be called either, since
// a profile's choices would show two by one name.
EditRefusal addCustomTone(AppSettings &settings, CustomTone tone, const QStringList &builtInNames);
EditRefusal editCustomTone(AppSettings &settings, const QString &id, const EntryChange<CustomTone> &change,
                           const QStringList &builtInNames);
// The same for a custom cleanup level, based on light_cleanup, balanced,
// strong_polish or custom_only; a Custom only level needs instructions.
EditRefusal addCustomCleanupLevel(AppSettings &settings, CustomCleanupLevel level, const QStringList &builtInNames);
EditRefusal editCustomCleanupLevel(AppSettings &settings, const QString &id,
                                   const EntryChange<CustomCleanupLevel> &change, const QStringList &builtInNames);

// Removes the custom tone id, and every profile that chose it gets no tone.
// Returns the edit that puts the tone back where it was and points back the
// profiles that still have no tone, so changes made in between stay; it does
// nothing once a tone by that name was added since. Empty for an id the
// settings do not hold.
SettingsEdit removeCustomTone(AppSettings &settings, const QString &id);
// The same for a custom cleanup level. Its profiles get the built-in level it
// was based on, or Medium for one based on no level (Custom only).
SettingsEdit removeCustomCleanupLevel(AppSettings &settings, const QString &id);

// The user's app matches. Each edit first folds the per-application profile
// overrides they replaced into them, which retires the overrides for good. A
// match needs an app no other match names (ignoring case), and an app type,
// an offered profile, or both. `match` finds the one edited, ignoring case.
EditRefusal addAppRecognitionRule(AppSettings &settings, AppRecognitionRule rule);
EditRefusal editAppRecognitionRule(AppSettings &settings, const QString &match,
                                   const EntryChange<AppRecognitionRule> &change);
// Removes the match; the edit it returns puts it back unless a match for the
// app was added since. Empty when no match names it.
SettingsEdit removeAppRecognitionRule(AppSettings &settings, const QString &match);

} // namespace speecher
