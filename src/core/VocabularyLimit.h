#pragma once

#include <QString>
#include <QStringList>

namespace speecher::VocabularyLimit {

constexpr int maxKeyterms = 100;
constexpr int maxTokens = 500;
// Whisper reads at most 223 prompt tokens and drops the start of a longer
// prompt, which is where the highest-priority terms are. Proper nouns run
// about 2.5 characters a token, so this stays inside that.
constexpr int maxPromptChars = 500;

int tokenCount(const QString &term);
int tokenCount(const QStringList &terms);
// The prefix of `terms` a request can carry, within both caps.
QStringList limited(const QStringList &terms);
// What the stored list amounts to, for a reader: the whole count, and how much
// of it a request carries when the list is over either cap.
QString summary(const QStringList &terms);
// The terms as a speech model's prompt: a comma-separated list of whole terms,
// in order, within maxPromptChars.
QString promptText(const QStringList &terms);

} // namespace speecher::VocabularyLimit
