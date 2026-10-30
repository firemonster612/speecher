#pragma once

// How a message names the providers that did not do their part, and why,
// from their ProviderAttemptIssues: shared by the popup's outcomes and the
// speech failures sessions and recordings report.

#include "core/ProviderAttemptIssue.h"
#include "core/ProviderChain.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace speecher {

// "A", "A and B", "A, B and C".
QString joinedWithAnd(const QStringList &items);
// Whether the service turned down a provider's sign-in, rather than a key.
bool rejectedSignIn(const ProviderAttemptIssue &issue);
// One sentence naming each provider and why, providers with the same reason
// sharing it: "OpenAI and Local Runner couldn't be reached." Empty for none.
QString shortfallSentence(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels);
// "Your ChatGPT sign-in has expired." once for each sign-in turned down.
QStringList signInSentences(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels);
// The error when no speech provider in the chain could take the audio,
// naming each and why.
QString noSpeechServiceText(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels);

} // namespace speecher
