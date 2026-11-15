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
// case (and for ids, a ".desktop" or ".exe" ending); Incomplete when steps
// of its own leave none on; Missing when it picks an App Type that isn't
// one, or a profile that isn't offered. An App with no name takes its first
// match's text.
EditRefusal addApp(AppSettings &settings, AppEntry app);
// Changes App id as change says, with the same rules. Missing when no App
// has that id.
EditRefusal editApp(AppSettings &settings, const QString &id, const EntryChange<AppEntry> &change);
// Removes App id; the edit it returns puts it back where it was, unless an
// App with its id or one of its matches was added since, and the Vocabulary
// limits removing it rewrote (keepVocabularyLimitsOf). Empty when no App has
// that id.
SettingsEdit removeApp(AppSettings &settings, const QString &id);
// A new custom type's id, or the reason its name was refused.
struct AddAppTypeResult {
    QString id;
    EditRefusal refusal = EditRefusal::None;
};

// Adds an empty custom type before Other apps. Blank for an empty trimmed
// name; Taken for another custom type's name, ignoring case.
AddAppTypeResult addAppType(AppSettings &settings, const QString &name);
// Changes a type by id, with the same name checks for custom types.
// Missing for an absent id, a built-in name change or an unavailable
// profile; Incomplete when its own steps leave none on.
EditRefusal editAppType(AppSettings &settings, const QString &id, const EntryChange<AppTypeSettings> &change);
// Removes a custom type and returns its Apps to Automatic. Undo restores
// its place and membership for Apps still on Automatic. Empty for built-ins
// or an absent id. A type added with its id or name meanwhile stays.
SettingsEdit removeAppType(AppSettings &settings, const QString &id);

} // namespace speecher
