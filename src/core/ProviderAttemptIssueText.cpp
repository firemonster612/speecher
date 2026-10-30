#include "core/ProviderAttemptIssueText.h"

#include "core/ProviderReadiness.h"
#include "core/ProviderTable.h"

namespace speecher {

namespace {

using Stage = ProviderAttemptIssue::Stage;

// What follows a provider's name to say why it did not do its part.
QString shortfall(const ProviderAttemptIssue &issue)
{
    switch (issue.stage) {
    case Stage::LanguageSkipped:
        return QStringLiteral("can't listen for your Spoken Language");
    case Stage::Interrupted:
        return QStringLiteral("dropped");
    case Stage::Prepare:
    case Stage::Connect:
        break;
    }
    switch (issue.kind) {
    case ProviderFailureKind::Network:
        return QStringLiteral("couldn't be reached");
    case ProviderFailureKind::Timeout:
        return QStringLiteral("didn't answer");
    case ProviderFailureKind::Server:
        return QStringLiteral("had a server error");
    case ProviderFailureKind::RateLimited:
        return QStringLiteral("hit a usage limit");
    case ProviderFailureKind::Authentication:
        return QStringLiteral("turned down its key");
    case ProviderFailureKind::Unavailable:
        if (issue.role == ProviderRole::Speech && parseProviderId(issue.providerId) == ProviderId::Local) {
            return issue.stage == Stage::Prepare ? QStringLiteral("has no model downloaded")
                                                 : QStringLiteral("couldn't load its model");
        }
        // Unavailable says nothing about a sign-in: the keyring may not have
        // answered. Only a turned-down one is named as such.
        if (!providerSignsIn(issue.providerId)) {
            return QStringLiteral("isn't set up");
        }
        break;
    case ProviderFailureKind::Other:
    case ProviderFailureKind::InvalidResult:
    case ProviderFailureKind::Cancelled:
        break;
    }
    return QStringLiteral("couldn't start");
}

} // namespace

QString joinedWithAnd(const QStringList &items)
{
    if (items.size() < 2) {
        return items.value(0);
    }
    return items.first(items.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + items.last();
}

bool rejectedSignIn(const ProviderAttemptIssue &issue)
{
    return issue.kind == ProviderFailureKind::Authentication && providerSignsIn(issue.providerId);
}

QString shortfallSentence(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels)
{
    QStringList reasons;
    QList<QStringList> names;
    for (const ProviderAttemptIssue &issue : issues) {
        const QString reason = shortfall(issue);
        if (!reasons.contains(reason)) {
            reasons.append(reason);
            names.append(QStringList());
        }
        names[reasons.indexOf(reason)].append(labels(issue.role, issue.providerId));
    }
    QStringList clauses;
    for (qsizetype index = 0; index < reasons.size(); ++index) {
        clauses.append(joinedWithAnd(names[index]) + QLatin1Char(' ') + reasons[index]);
    }
    return clauses.isEmpty() ? QString() : joinedWithAnd(clauses) + QLatin1Char('.');
}

QStringList signInSentences(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels)
{
    QStringList sentences;
    for (const ProviderAttemptIssue &issue : issues) {
        const QString sentence = QStringLiteral("Your %1 sign-in has expired.")
                                     .arg(signInName(issue.providerId, labels(issue.role, issue.providerId)));
        if (rejectedSignIn(issue) && !sentences.contains(sentence)) {
            sentences.append(sentence);
        }
    }
    return sentences;
}

QString noSpeechServiceText(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels)
{
    QStringList sentences{QStringLiteral("No speech service is available.")};
    const QList<ProviderAttemptIssue> notSignIns =
        issuesWhere(issues, [](const ProviderAttemptIssue &issue) { return !rejectedSignIn(issue); });
    const QString sentence = shortfallSentence(notSignIns, labels);
    if (!sentence.isEmpty()) {
        sentences.append(sentence);
    }
    return (sentences + signInSentences(issues, labels)).join(QLatin1Char(' '));
}

} // namespace speecher
