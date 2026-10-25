#pragma once

#include "core/SettingsEdit.h"

// The edits the user makes to a draft of the settings' vocabulary terms,
// Learned Corrections and replacements: adding and changing one, refused
// with the reason when the list would hold something it can't, and removing
// one with the edit that puts it back. Each finds its entry by what the list
// shows for it, not by place, since dictation reorders terms and adds
// corrections while a form is open.

namespace speecher {

// Adds a term the user typed. It needs text no other term has, ignoring
// case; its profiles keep only the ones the settings hold.
EditRefusal addVocabularyEntry(AppSettings &settings, VocabularyEntry entry);
// Changes the term `term` (ignoring case) as change says, with the same
// rules. What change leaves alone, such as its uses, stays.
EditRefusal editVocabularyEntry(AppSettings &settings, const QString &term,
                                const EntryChange<VocabularyEntry> &change);
// Removes the term; the edit it returns puts it back unless the term was
// added again since, whose values then stay. Empty when no term matches.
SettingsEdit removeVocabularyEntry(AppSettings &settings, const QString &term);
// Merges the terms of an import into the list, as normalizeVocabularyEntries
// merges two copies of one term: a term listed already keeps its own key
// term, context and profiles, takes a context it lacks and the higher use
// count. Returns how many terms are new.
int importVocabularyEntries(AppSettings &settings, const QList<VocabularyEntry> &incoming);

// Changes the Learned Correction id as change says. It needs what was heard
// (Blank otherwise) and what it is corrected to (Incomplete), and no other
// correction may correct the same words in the same app (Taken). What it was
// learned from stays.
EditRefusal editLearnedCorrection(AppSettings &settings, const QString &id,
                                  const EntryChange<LearnedCorrection> &change);
// Removes the correction; the edit it returns puts it back unless dictation
// learned it again since. Empty when no correction has that id.
SettingsEdit removeLearnedCorrection(AppSettings &settings, const QString &id);

// Adds a replacement: a spoken phrase with a letter or number in it, no
// other replacement's once normalized (Taken), and text to write
// (Incomplete).
EditRefusal addBindingRule(AppSettings &settings, BindingRule rule);
// Changes the replacement for `phrase` (as normalized) as change says.
EditRefusal editBindingRule(AppSettings &settings, const QString &phrase, const EntryChange<BindingRule> &change);
// Removes it; the edit it returns puts it back unless the phrase was given a
// replacement again since. Empty when no replacement has the phrase.
SettingsEdit removeBindingRule(AppSettings &settings, const QString &phrase);
// Adds each imported replacement addBindingRule would take, skipping the
// rest, and returns how many it added.
int importBindingRules(AppSettings &settings, const QList<BindingRule> &incoming);

} // namespace speecher
