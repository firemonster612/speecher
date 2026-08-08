#pragma once

#include "core/AppSettings.h"

#include <QByteArray>
#include <QList>

namespace speecher {

QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries);
QList<VocabularyEntry> parseVocabularyCsv(const QByteArray &csv, QString *error = nullptr);

} // namespace speecher
