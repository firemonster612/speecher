#pragma once

#include "core/AppSettings.h"

#include <QByteArray>
#include <QList>

namespace speecher {

// Trims, deduplicates case-insensitively, and orders by send priority: starred
// first, then most used, then most recent. Keeps every entry; the service cap
// is applied where terms are sent, not where they are stored.
QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries);
QList<VocabularyEntry> parseVocabularyCsv(const QByteArray &csv, QString *error = nullptr);
bool containsVocabularyTerm(const QString &text, const QString &term);

} // namespace speecher
