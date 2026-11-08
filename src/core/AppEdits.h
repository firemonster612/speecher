#pragma once

#include "core/SettingsEdit.h"

// The edits the person makes to a draft's Apps and App Types: adding and
// changing an App, refused with the reason when the list can't hold it,
// removing one with the edit that puts it back, and changing what an App
// Type sets.

namespace speecher {

// Adds an App after the others, with a fresh id when it has none or one
// taken. Blank when nothing is left to match it by once empty matches are
// dropped; Taken when another App has one of its matches already, ignoring
// case (and for ids, a ".desktop" or ".exe" ending); Missing when it picks
// an App Type that isn't one, or a profile that isn't offered. An App with
// no name takes its first match's text.
EditRefusal addApp(AppSettings &settings, AppEntry app);
// Changes App id as change says, with the same rules. Missing when no App
// has that id.
EditRefusal editApp(AppSettings &settings, const QString &id, const EntryChange<AppEntry> &change);
// Removes App id; the edit it returns puts it back where it was, unless an
// App with its id or one of its matches was added since. Vocabulary Entries
// limited to it keep the limit by its ids (keepVocabularyLimitsOf). Empty when no App
// has that id.
SettingsEdit removeApp(AppSettings &settings, const QString &id);
// Changes what App Type `type` sets. Missing for a type that isn't one of
// appTypes(), or a profile that isn't offered. Standard paste is no choice
// of its own for a type, so it reads as the steps.
EditRefusal editAppType(AppSettings &settings, AppCategory type, const EntryChange<AppTypeSettings> &change);

} // namespace speecher
