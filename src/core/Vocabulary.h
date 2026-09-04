#pragma once

#include "core/AppSettings.h"

#include <QByteArray>
#include <QList>

namespace speecher {

QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries);
QList<VocabularyEntry> parseVocabularyCsv(const QByteArray &csv, QString *error = nullptr);
bool containsVocabularyTerm(const QString &text, const QString &term);

} // namespace speecher
