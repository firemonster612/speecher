#pragma once

#include "core/AppSettings.h"

#include <QSettings>

// The Vocabulary as vocabulary/entries stores it: one JSON list of every
// entry, spellings, replacements and Learned Corrections alike.

namespace speecher::VocabularySettingsCodec {

// The stored entries, normalized, each with an id. A replacement the
// dictation could not apply is dropped.
QList<VocabularyEntry> load(const QSettings &settings);
void store(QSettings &settings, const QList<VocabularyEntry> &entries);
// Counts a use of every spelling the text holds. Whether it held one.
bool recordUsage(QSettings &settings, const QString &text);

} // namespace speecher::VocabularySettingsCodec
