#pragma once

#include <QSettings>

// The migration that moves the three word lists of older releases into the
// one Vocabulary. The old keys stay in the file, unwritten, so a downgrade
// within a release still reads them.

namespace speecher {

// Moves the vocabulary terms (stt/vocabularyEntries, or the older
// stt/customVocabulary), the replacements (bindings/rules) and the Learned
// Corrections (vocabulary/learnedCorrections) into vocabulary/entries, once,
// as migration/vocabularyVersion records:
//  1. each term becomes a spelling with its context, profiles, Key term and
//     uses;
//  2. each replacement becomes a replacement for every profile and app;
//  3. each Learned Correction becomes a learned replacement limited to the
//     app it was learned in, none for one learned in several, keeping its
//     id, whether it is on, and what it was learned from.
// Whatever an older release would not have loaded stays behind. A file that
// stored none of these lists stores none.
void migrateVocabulary(QSettings &settings);

} // namespace speecher
