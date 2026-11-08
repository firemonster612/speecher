#pragma once

#include "core/PasteMethod.h"

#include <QList>
#include <QSettings>
#include <QString>

// What older releases stored about apps, read only to move it into the Apps
// settings, and the migration that moves it. The old keys stay in the file,
// unwritten, so a downgrade within a release still reads them.

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

// output/pasteRules as stored. Nothing stored, or no list, reads as the rule
// that was the default: Terminals press their own keys where those differ.
QList<LegacyPasteRule> legacyPasteRules(const QSettings &settings);
void storeLegacyPasteRules(QSettings &settings, const QList<LegacyPasteRule> &rules);

// Moves the per-app settings of older releases into apps/entries and
// apps/types, once, as migration/appsVersion records:
//  1. each Writing Profile override turned on becomes an App matched by its
//     id, with that profile;
//  2. each single-app paste rule turned on gives the App matched by the same
//     id its Pasting choice, adding one where there is none;
//  3. each recognition rule becomes an App matched by name, and by window
//     title too where it set a profile, since profile rules looked at titles
//     and type rules didn't;
//  4. app types' paste rules become the App Types' Pasting choices;
//  5. the default Writing Profile becomes Other apps' profile.
// A file that stored none of these keeps reading the defaults. Each Pasting
// choice becomes steps and paste keys that deliver as it did with the
// Pasting page's steps as stored; a file the first version of this
// migration wrote has its choices turned into steps and keys the same way.
void migrateApps(QSettings &settings);

} // namespace speecher
