#pragma once

#include "core/SettingsEdit.h"

// The edits the person makes to a draft of the settings' Vocabulary: adding
// and changing an entry, refused with the reason when the list would hold
// something it can't, and removing one with the edit that puts it back. Each
// finds its entry by id, not by place, since dictation reorders spellings
// and adds Learned Corrections while a form is open.

namespace speecher {

// Adds an entry, with a fresh id. A spelling needs a term (Blank) no other
// spelling has, ignoring case (Taken). A replacement needs a phrase with a
// letter or number in it (Blank), and no other replacement may have that
// phrase, once normalized, under the same limits (Taken). Its profiles keep
// only the ones the settings hold; an App it is limited to must be one of
// the settings' Apps (Missing).
EditRefusal addVocabularyEntry(AppSettings &settings, VocabularyEntry entry);
// Changes the entry `id` as change says, with the same rules, and a Learned
// Correction must keep text to write (Incomplete). What change leaves alone,
// such as its uses or what it was learned from, stays.
EditRefusal editVocabularyEntry(AppSettings &settings, const QString &id,
                                const EntryChange<VocabularyEntry> &change);
// Removes the entry; the edit it returns puts it back unless one it would
// clash with was added since, whose values then stay. Empty when no entry
// has that id.
SettingsEdit removeVocabularyEntry(AppSettings &settings, const QString &id);
// Merges an import into the list: a spelling listed already keeps its own
// Key term, context and limits, takes a context it lacks and the higher use
// count, as normalizeVocabularyEntries merges two copies; each replacement
// addVocabularyEntry would take is added, the rest skipped. Returns how many
// entries are new.
int importVocabularyEntries(AppSettings &settings, const QList<VocabularyEntry> &incoming);
// Keeps the entries limited to App `removed`, which is going, limited to the
// app ids it matched, so a snippet for one app does not start writing
// everywhere. A limit to an App matched only by name or window title goes.
void keepVocabularyLimitsOf(AppSettings &settings, const AppEntry &removed);

} // namespace speecher
