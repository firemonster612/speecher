#pragma once

#include "core/DeliverySteps.h"
#include "core/PasteMethod.h"

#include <QList>
#include <QSettings>
#include <QString>

// What older releases stored about apps and paste rules, read only to move
// it into the Apps settings and the delivery steps, and the migration that
// moves it into Apps. The old keys stay in the file, unwritten, so a
// downgrade within a release still reads them.

namespace speecher {

// A paste rule as output/pasteRules stored it.
struct LegacyPasteRule {
    enum class Scope { Global, Category, Application };

    Scope scope = Scope::Global;
    // An app type's name, or an application id.
    QString match;
    PasteMethod method = PasteMethod::StandardPaste;
    bool enabled = true;

    bool operator==(const LegacyPasteRule &other) const = default;
};

// output/pasteRules as stored. Nothing stored, no list or an empty one
// reads as the rules that were the default, as older releases read them:
// Terminals' Terminal paste and a global Standard paste. A list without a
// global rule turned on copied only, everywhere no other rule applied.
QList<LegacyPasteRule> legacyPasteRules(const QSettings &settings);
void storeLegacyPasteRules(QSettings &settings, const QList<LegacyPasteRule> &rules);

// The steps a delivery took under the global paste rule, output/method and
// output/restoreClipboardAfterTyping as older releases stored them: the
// Pasting page's steps once migrated (migrateDeliverySteps).
DeliverySteps legacyDeliverySteps(const QSettings &settings);

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

} // namespace speecher
