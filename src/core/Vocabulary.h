#pragma once

#include "core/AppSettings.h"

#include <QByteArray>
#include <QList>

#include <optional>

namespace speecher {

// Tidies each entry and drops one with no term. Spellings come first, one
// per term ignoring case, in send priority: starred key terms, then most
// used, then most recent. Replacements follow in their own order, each kept:
// two may share a phrase under different limits. Keeps every spelling; the
// service cap is applied where terms are sent, not where they are stored.
QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries);
// The entries, each without an id given a fresh one.
QList<VocabularyEntry> withVocabularyIds(QList<VocabularyEntry> entries);
QList<VocabularyEntry> parseVocabularyCsv(const QByteArray &csv, QString *error = nullptr);
bool containsVocabularyTerm(const QString &text, const QString &term);
// The entry's profiles without the ones `profiles` no longer holds. One left
// with none applies to every profile, as a recognition rule whose profile was
// deleted loses it.
QStringList offeredVocabularyProfiles(const QStringList &ids,
                                      const QList<WritingProfileSettings> &profiles);
// The terms in a vocabulary file, one per line, skipping blank lines and ones
// starting with #.
QStringList parseVocabularyFile(const QByteArray &text);
// parseVocabularyFile for the file at `path`. Nothing when it cannot be read.
std::optional<QStringList> readVocabularyFile(const QString &path);

} // namespace speecher
