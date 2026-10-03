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
// What Claude Voice's x-config-keyterms header carries from `terms`, in
// order: each spelling once ignoring ASCII case, and no more than fit in its
// 1024 bytes of UTF-8, which is how the service reads the header.
QStringList claudeVoiceKeyterms(const QStringList &terms);
// The terms the chosen speech service receives from `terms`: none for one
// that takes no hints, the capped prefix for a custom endpoint, and that
// prefix as the Claude Voice header can carry it for Claude.
QStringList speechKeyterms(const QStringList &terms, const QString &speechProviderId);
// What the stored list amounts to, for a reader: the whole count, how much of
// it goes to the speech service, and how much to refinement. `keyTerms` are
// the ones marked to go to the speech service, in priority order.
QString summary(const QStringList &terms, const QStringList &keyTerms, const QString &speechProviderId);
// The same where every term is a key term.
QString summary(const QStringList &terms, const QString &speechProviderId);

} // namespace speecher::VocabularyLimit
