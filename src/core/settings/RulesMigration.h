#pragma once

#include <QSettings>

// The migration that turns the four kinds of refinement prose older releases
// kept into the user's rules. The retired keys stay in the file, unwritten,
// so a downgrade still reads them.

namespace speecher {

// Makes rules into refinement/rules, once, as migration/rulesVersion
// records, in this order:
//  1. the global instructions (refinement/additionalInstructions), when not
//     blank: "Instructions for every profile", on in every profile;
//  2. each Writing Profile's instructions that are not blank, in stored
//     order: "<profile> instructions", on in that profile;
//  3. each custom tone: "<name> tone", telling refinement to write in it, on
//     in each profile that chose it, which then has no tone;
//  4. each custom cleanup level: its name and instructions, on in each
//     profile that chose it, which then has the level's base, or Light for
//     one based on Custom only.
// A name already taken gets " 2", " 3" and so on. A file that already stores
// rules is left as it is.
void migrateRules(QSettings &settings);

} // namespace speecher
