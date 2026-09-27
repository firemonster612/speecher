#pragma once

#include <QString>
#include <QStringList>

namespace speecher::VocabularyLimit {

constexpr int maxKeyterms = 100;
constexpr int maxTokens = 500;

int tokenCount(const QString &term);
int tokenCount(const QStringList &terms);
// The prefix of `terms` a request can carry, within both caps.
QStringList limited(const QStringList &terms);
// What the stored list amounts to, for a reader: the whole count, and how much
// of it a request carries when the list is over either cap.
QString summary(const QStringList &terms);

} // namespace speecher::VocabularyLimit
