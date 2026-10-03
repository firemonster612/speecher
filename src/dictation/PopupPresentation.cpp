#include "dictation/PopupPresentation.h"

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
                           const QString &prefix,
                           const std::function<bool(const QString &)> &fits)
{
    // Later starts are shorter tails, so the first that fits is the longest.
    qsizetype low = 1;
    qsizetype high = starts.size();
    while (low < high) {
        const qsizetype middle = (low + high) / 2;
        if (fits(prefix + text.mid(starts[middle]).trimmed())) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    if (low < starts.size()) {
        const QString tail = prefix + text.mid(starts[low]).trimmed();
        if (fits(tail)) {
            return tail;
        }
    }
    return {};
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

QString trimPreviewToFit(const QString &preview, const std::function<bool(const QString &)> &fits)
{
    const QString whole = preview.simplified();
    if (whole.isEmpty() || fits(whole)) {
        return whole;
    }
    const QString atWord = longestFittingTail(
        whole, boundaries(QTextBoundaryFinder::Word, whole, true), QStringLiteral("… "), fits);
    if (!atWord.isEmpty()) {
        return atWord;
    }
    // Not even the last word fits: keep as much of its end as does.
    const QString atGrapheme = longestFittingTail(
        whole, boundaries(QTextBoundaryFinder::Grapheme, whole, false), QStringLiteral("…"), fits);
    return atGrapheme.isEmpty() ? QStringLiteral("…") : atGrapheme;
}

} // namespace speecher
