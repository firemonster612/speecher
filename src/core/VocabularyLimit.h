#pragma once

#include <QString>
#include <QStringList>

namespace speecher::VocabularyLimit {

constexpr int maxKeyterms = 100;
constexpr int maxTokens = 500;
// Refinement reads the list as prompt text, so it takes far more than the
// speech service does; the ceiling only keeps the prompt bounded.
constexpr int maxRefinementTerms = 1000;

int tokenCount(const QString &term);
int tokenCount(const QStringList &terms);
// The prefix of `terms` a speech request can carry, within both caps.
QStringList limited(const QStringList &terms);
// What the stored list amounts to, for a reader: the whole count, how much of
// it goes to the speech service, and how much to refinement. A service that
// takes no hints gets none, so only refinement is counted.
QString summary(const QStringList &terms, bool speechTakesHints = true);

} // namespace speecher::VocabularyLimit
