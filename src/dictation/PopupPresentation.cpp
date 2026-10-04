#include "dictation/PopupPresentation.h"

#include "core/settings/FallbackPresentation.h"
#include "core/settings/SettingsSchema.h"

#include <QList>
#include <QTextBoundaryFinder>

#include <algorithm>

namespace speecher {

namespace {

// Where the text may be cut, in order: each position a word starts at, which
// Unicode puts between CJK words as well as after spaces.
QList<qsizetype> boundaries(QTextBoundaryFinder::BoundaryType type, const QString &text,
                            bool wordStartsOnly)
{
    QList<qsizetype> starts;
    QTextBoundaryFinder finder(type, text);
    for (qsizetype position = 0; position >= 0 && position < text.size();
         position = finder.toNextBoundary()) {
        if (!wordStartsOnly || finder.boundaryReasons().testFlag(QTextBoundaryFinder::StartOfItem)
            || position == 0) {
            starts.append(position);
        }
    }
    return starts;
}

// The longest tail from these cut points that fits, or empty. Never the whole
// text (the first cut point), which the caller already found too wide.
QString longestFittingTail(const QString &text, const QList<qsizetype> &starts,
                           const std::function<bool(const QString &)> &fits)
{
    // Later starts are shorter tails, so the first that fits is the longest.
    qsizetype low = 1;
    qsizetype high = starts.size();
    while (low < high) {
        const qsizetype middle = (low + high) / 2;
        if (fits(text.mid(starts[middle]).trimmed())) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    if (low < starts.size()) {
        const QString tail = text.mid(starts[low]).trimmed();
        if (fits(tail)) {
            return tail;
        }
    }
    return {};
}

using Stage = ProviderAttemptIssue::Stage;

// "A", "A and B", "A, B and C".
QString joinedList(const QStringList &items)
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
        if (issue.role == ProviderRole::Speech && issue.providerId == QStringLiteral("local")) {
            return issue.stage == Stage::Prepare ? QStringLiteral("has no model downloaded")
                                                 : QStringLiteral("couldn't load its model");
        }
        return providerSignsIn(issue.providerId) ? QStringLiteral("isn't signed in") : QStringLiteral("isn't set up");
    case ProviderFailureKind::Other:
    case ProviderFailureKind::InvalidResult:
    case ProviderFailureKind::Cancelled:
        break;
    }
    return QStringLiteral("couldn't start");
}

// One sentence naming each provider and why, providers with the same reason
// sharing it: "OpenAI and Local Runner couldn't be reached." Empty for none.
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
        clauses.append(joinedList(names[index]) + QLatin1Char(' ') + reasons[index]);
    }
    return clauses.isEmpty() ? QString() : joinedList(clauses) + QLatin1Char('.');
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

QList<ProviderAttemptIssue> issuesWhere(const QList<ProviderAttemptIssue> &issues,
                                        const std::function<bool(const ProviderAttemptIssue &)> &keep)
{
    QList<ProviderAttemptIssue> kept;
    std::copy_if(issues.cbegin(), issues.cend(), std::back_inserter(kept), keep);
    return kept;
}

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
    *wordsMayBeMissing = speechInterrupted;

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
        const QString note = QStringLiteral("No internet, so %1 did this one.").arg(joinedList(names));
        return speechInterrupted ? note + QStringLiteral(" A few words may be missing.") : note;
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
                                     .arg(joinedList(dropped), usedName));
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
        sentences += signInSentences(issues, labels);
    }
    return sentences.join(QLatin1Char(' '));
}

// About how long a reader needs per character, past the minimum.
constexpr int kPopupErrorMsPerCharacter = 60;
// A server's whole error body should not hold the popup for minutes.
constexpr int kPopupErrorMaximumMs = 30000;

} // namespace

QString popupDismissCaption()
{
    return QStringLiteral("Dismiss");
}

QString cancelledOutcomeText()
{
    return QStringLiteral("Canceled");
}

QString renewingSignInText()
{
    return QStringLiteral("Renewing sign-in\u2026");
}

int popupErrorDismissMs(const QString &message)
{
    return std::clamp(int(message.simplified().size()) * kPopupErrorMsPerCharacter,
                      kPopupErrorMinimumMs,
                      kPopupErrorMaximumMs);
}

QString popupErrorActionLabel(const PopupErrorAction &action)
{
    switch (action.fix) {
    case ErrorFix::None:
        break;
    case ErrorFix::SettingsPage:
        return QStringLiteral("Open %1").arg(paneTitle(action.pageId));
    case ErrorFix::MicrophonePermission:
        // ui-lint: allow title-case: Microphone is the name of the system's privacy pane.
        return QStringLiteral("Open Microphone settings");
    case ErrorFix::AccessibilityPermission:
        return accessibilityGrantActionLabel();
    }
    return {};
}

DictationOutcome dictationOutcome(const QString &receipt,
                                  bool copiedOnly,
                                  const ProviderHistory &history,
                                  bool usedRawTranscript,
                                  const QString &speechWarning,
                                  const ProviderLabels &labels)
{
    const QString separator = QStringLiteral(" • ");
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

QString noSpeechServiceText(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels)
{
    QStringList sentences{QStringLiteral("No speech service is available.")};
    const QString sentence = shortfallSentence(
        issuesWhere(issues, [](const ProviderAttemptIssue &issue) { return !rejectedSignIn(issue); }), labels);
    if (!sentence.isEmpty()) {
        sentences.append(sentence);
    }
    return (sentences + signInSentences(issues, labels)).join(QLatin1Char(' '));
}

PopupErrorAction speechSetupAction(const QString &providerId)
{
    if (providerId == QStringLiteral("local")) {
        return {ErrorFix::SettingsPage, QStringLiteral("localModels")};
    }
    if (providerId == QStringLiteral("endpoint")) {
        return {ErrorFix::SettingsPage, QStringLiteral("dictation")};
    }
    return {ErrorFix::SettingsPage, QStringLiteral("accounts")};
}

PreviewLine trimPreviewToFit(const QString &preview, const std::function<bool(const QString &)> &fits)
{
    const QString whole = preview.simplified();
    if (whole.isEmpty() || fits(whole)) {
        return {whole, false};
    }
    const QString atWord = longestFittingTail(
        whole, boundaries(QTextBoundaryFinder::Word, whole, true), fits);
    if (!atWord.isEmpty()) {
        return {atWord, true};
    }
    // Not even the last word fits: keep as much of its end as does.
    return {longestFittingTail(whole, boundaries(QTextBoundaryFinder::Grapheme, whole, false), fits),
            true};
}

} // namespace speecher
