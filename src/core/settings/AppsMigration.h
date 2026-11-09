#pragma once

#include <QSettings>

// The migrations that move what older releases stored about apps, paste
// rules and delivery into the Apps settings and the Delivery Steps. The old
// keys stay in the file, unwritten, so a downgrade within a release still
// reads them.

namespace speecher {

// Moves the per-app settings of older releases into apps/entries and
// apps/types, once, as migration/appsVersion records:
//  1. each Writing Profile override turned on becomes an App matched by its
//     id, with that profile;
//  2. each single-app paste rule turned on gives the App matched by the same
//     id its Pasting choice, adding one where there is none;
//  3. each recognition rule becomes an App matched by name, and by window
//     title too where it set a profile, since profile rules looked at titles
//     and type rules didn't; one that set only a type still leaves the
//     profile to the catalog;
//  4. app types' paste rules become the App Types' Pasting choices;
//  5. the default Writing Profile becomes Other apps' profile.
// A file that stored none of these keeps reading the defaults. Each paste
// rule becomes steps and paste keys that deliver as it did under the older
// output settings, so this runs before migrateDeliverySteps removes them; a
// file the first version of this migration wrote has its Pasting choices
// narrow the Pasting page's steps as stored, as they did then.
void migrateApps(QSettings &settings);

// Delivery was once a mode (output/method), a global paste rule and a
// clipboard restore switch; the three delivery steps replace all of them.
// Each stored choice turns off the steps it never used: Automatic keeps them
// all, a keyboard-only method turns Insert off, Direct insertion turns Press
// off, a clipboard method or a Clipboard only global rule turns both off,
// and restoring the clipboard turns Keep off. Runs once, as its marker
// records, and only changes a file that stored one of the three; it removes
// the method and the switch, and leaves the rules for a downgrade.
// migrateApps runs before it.
void migrateDeliverySteps(QSettings &settings);

} // namespace speecher
