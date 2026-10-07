#pragma once

#include "core/AppSettings.h"

#include <QByteArray>
#include <QList>

#include <optional>

namespace speecher {

// Trims, deduplicates case-insensitively, and orders by send priority: key
// terms with Priority first, then most used, then most recent. Keeps every entry; the service cap
// is applied where terms are sent, not where they are stored.
QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries);
QList<VocabularyEntry> parseVocabularyCsv(const QByteArray &csv, QString *error = nullptr);
bool containsVocabularyTerm(const QString &text, const QString &term);
// Whether the entry takes part in a dictation under the Writing Profile: it is
// limited to none, or to that one among others.
bool vocabularyEntryApplies(const VocabularyEntry &entry, const QString &writingProfile);
// Whether `entries` limit `term` (ignoring case) to profiles other than
// `writingProfile`, which keeps it out even as a Learned Correction's text.
bool vocabularyTermExcluded(const QList<VocabularyEntry> &entries,
                            const QString &term,
                            const QString &writingProfile);
// The terms a speech request carries under the Writing Profile: the key terms
// that apply to it, then the enabled Learned Corrections for terms the list
// does not hold, within the caps. An
// empty profile takes every entry, for a request made before one is known.
QStringList speechVocabulary(const QList<VocabularyEntry> &entries,
                             const QList<LearnedCorrection> &corrections,
                             const QString &writingProfile);
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
// `entries` with `terms` added for one run, for every profile. A term already
// listed loses its profile limit, so refinement gets it whatever the saved
// entry says; the speech service gets the terms through the caller, ahead of
// the saved ones.
QList<VocabularyEntry> withAddedVocabulary(QList<VocabularyEntry> entries, const QStringList &terms);

} // namespace speecher
