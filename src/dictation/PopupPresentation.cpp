#include "dictation/PopupPresentation.h"

#include "core/ProviderAttemptIssueText.h"
#include "core/ProviderTable.h"

#include <QList>

#include <algorithm>

namespace speecher {

namespace {

using Stage = ProviderAttemptIssue::Stage;

// The provider that did a role's work in place of one that let it down: the
// last that ran, unless it failed too or the raw transcript stood in.
QString standIn(const QStringList &ran, const QList<ProviderAttemptIssue> &roleIssues, bool rawTranscriptStoodIn)
{
    if (ran.isEmpty() || roleIssues.isEmpty() || rawTranscriptStoodIn) {
        return {};
    }
    const bool lastFailed = std::any_of(roleIssues.cbegin(), roleIssues.cend(), [&ran](const ProviderAttemptIssue &issue) {
        return issue.providerId == ran.last();
    });
    return lastFailed ? QString() : ran.last();
}

// What the fallbacks did and why, after the receipt. wordsMayBeMissing says
// whether it already warns that a speech switch lost words.
QString fallbackNote(const ProviderHistory &history, bool usedRawTranscript, const ProviderLabels &labels,
                     bool *wordsMayBeMissing)
{
    const auto inRole = [&history](ProviderRole role) {
        return issuesWhere(history.issues, [role](const ProviderAttemptIssue &issue) { return issue.role == role; });
    };
    const QList<ProviderAttemptIssue> speechIssues = inRole(ProviderRole::Speech);
    const QList<ProviderAttemptIssue> refinementIssues = inRole(ProviderRole::Refinement);
    const QString speechStandIn = standIn(history.speechRan, speechIssues, false);
    const QString refinementStandIn = standIn(history.refinementRan, refinementIssues, usedRawTranscript);
    const bool speechInterrupted = !speechStandIn.isEmpty()
        && std::any_of(speechIssues.cbegin(), speechIssues.cend(),
                       [](const ProviderAttemptIssue &issue) { return issue.stage == Stage::Interrupted; });
    const bool speechWordsLost = speechInterrupted
        || (!speechStandIn.isEmpty()
            && std::any_of(speechIssues.cbegin(), speechIssues.cend(),
                           [](const ProviderAttemptIssue &issue) { return issue.wordsLost; }));
    *wordsMayBeMissing = speechWordsLost;

    const bool allOffline = !history.issues.isEmpty()
        && std::all_of(history.issues.cbegin(), history.issues.cend(),
                       [](const ProviderAttemptIssue &issue) { return issue.offline; });
    if (allOffline && (!speechStandIn.isEmpty() || !refinementStandIn.isEmpty())) {
        QStringList names;
        if (!speechStandIn.isEmpty()) {
            names.append(labels(ProviderRole::Speech, speechStandIn));
        }
        if (!refinementStandIn.isEmpty()) {
            names.append(labels(ProviderRole::Refinement, refinementStandIn));
        }
        const QString note = QStringLiteral("No internet, so %1 did this one.").arg(joinedWithAnd(names));
        return speechWordsLost ? note + QStringLiteral(" A few words may be missing.") : note;
    }

    QStringList sentences;
    for (const ProviderRole role : {ProviderRole::Speech, ProviderRole::Refinement}) {
        const QList<ProviderAttemptIssue> &issues = role == ProviderRole::Speech ? speechIssues : refinementIssues;
        const QString used = role == ProviderRole::Speech ? speechStandIn : refinementStandIn;
        const QList<ProviderAttemptIssue> others = issuesWhere(issues, [](const ProviderAttemptIssue &issue) {
            return issue.stage != Stage::Interrupted && !rejectedSignIn(issue);
        });
        if (!used.isEmpty()) {
            const QString usedName = labels(role, used);
            if (role == ProviderRole::Speech && speechInterrupted) {
                QStringList dropped;
                for (const ProviderAttemptIssue &issue : issues) {
                    if (issue.stage == Stage::Interrupted) {
                        dropped.append(labels(role, issue.providerId));
                    }
                }
                sentences.append(QStringLiteral("%1 dropped, so %2 finished. A few words may be missing.")
                                     .arg(joinedWithAnd(dropped), usedName));
            } else if (others.isEmpty()) {
                sentences.append(QStringLiteral("Used %1.").arg(usedName));
            } else if (role == ProviderRole::Speech) {
                sentences.append(QStringLiteral("Transcribed with %1.").arg(usedName));
            } else {
                sentences.append(QStringLiteral("Cleaned up with %1.").arg(usedName));
            }
        }
        if (const QString sentence = shortfallSentence(others, labels); !sentence.isEmpty()) {
            sentences.append(sentence);
        }
        // The dropped sentence says so itself.
        if (role == ProviderRole::Speech && speechWordsLost && !speechInterrupted) {
            sentences.append(QStringLiteral("A few words may be missing."));
        }
        sentences += signInSentences(issues, labels);
    }
    return sentences.join(QLatin1Char(' '));
}

// About how long a reader needs per character, past the minimum.
constexpr int kPopupErrorMsPerCharacter = 60;
// A server's whole error body should not hold the popup for minutes.
constexpr int kPopupErrorMaximumMs = 30000;

} // namespace

QString popupMessageSeparator()
{
    return QStringLiteral(" • ");
}

QString cancelledOutcomeText()
{
    return QStringLiteral("Canceled");
}

int popupErrorDismissMs(const QString &message)
{
    return std::clamp(int(message.simplified().size()) * kPopupErrorMsPerCharacter,
                      kPopupErrorMinimumMs,
                      kPopupErrorMaximumMs);
}

DictationOutcome dictationOutcome(const QString &receipt,
                                  bool copiedOnly,
                                  const ProviderHistory &history,
                                  bool usedRawTranscript,
                                  const QString &speechWarning,
                                  const ProviderLabels &labels)
{
    const QString separator = popupMessageSeparator();
    bool wordsMayBeMissing = false;
    const QString note = fallbackNote(history, usedRawTranscript, labels, &wordsMayBeMissing);
    DictationOutcome outcome;
    outcome.message = usedRawTranscript ? QStringLiteral("Used raw transcript") + separator + receipt : receipt;
    if (!note.isEmpty()) {
        outcome.message += separator + note;
    }
    if (!speechWarning.isEmpty() && !wordsMayBeMissing) {
        outcome.message += separator + speechWarning;
    }
    outcome.outcome = usedRawTranscript || !history.issues.isEmpty() ? PopupOutcome::Fallback
        : copiedOnly                                                 ? PopupOutcome::Copied
                                                                     : PopupOutcome::Inserted;
    if (std::any_of(history.issues.cbegin(), history.issues.cend(), rejectedSignIn)) {
        outcome.fix = {ErrorFix::SettingsPage, QStringLiteral("accounts")};
    }
    return outcome;
}

PopupErrorAction speechSetupAction(const QString &providerId)
{
    const std::optional<ProviderId> provider = parseProviderId(providerId);
    if (provider == ProviderId::Local) {
        return {ErrorFix::SettingsPage, QStringLiteral("models")};
    }
    if (provider == ProviderId::Endpoint) {
        return {ErrorFix::SettingsPage, QStringLiteral("speech")};
    }
    return {ErrorFix::SettingsPage, QStringLiteral("accounts")};
}

} // namespace speecher
