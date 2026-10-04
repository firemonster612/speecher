#pragma once

#include <QMetaType>
#include <QString>

#include <functional>

namespace speecher {

// How a dictation ended, as the popup presents it. Each front end maps a kind
// to its own symbol; a clipboard-only receipt never shows a "sent" icon.
enum class PopupOutcome {
    // The text went into the Target.
    Inserted,
    // The text is on the clipboard only.
    Copied,
    // Refinement failed, so the raw transcript was delivered instead.
    Fallback,
    Error,
    // The person cancelled the session; nothing was delivered.
    Cancelled,
};

// What a cancelled session's popup says. It stays up as long as a delivered
// result does.
QString cancelledOutcomeText();

// Error text wraps at this width (logical pixels, points or DIPs) and the
// capsule grows taller rather than wider.
inline constexpr int kPopupErrorWrapWidth = 520;

// An error dismisses itself after popupErrorDismissMs, with a countdown bar
// beneath it: never sooner than this, and longer for a longer message so it
// can be read, up to 30 seconds.
inline constexpr int kPopupErrorMinimumMs = 8000;
int popupErrorDismissMs(const QString &message);

// The one thing an error can offer to fix it: a settings page, or the
// system's permission panel for what was refused.
enum class ErrorFix {
    None,
    SettingsPage,
    MicrophonePermission,
    AccessibilityPermission,
};
struct PopupErrorAction {
    ErrorFix fix = ErrorFix::None;
    // The settings page to open, for SettingsPage.
    QString pageId;
};
// The button that offers it, "Open Accounts"; empty for None.
QString popupErrorActionLabel(const PopupErrorAction &action);
// Where a speech service that cannot start is set up: Accounts for a
// sign-in, Local models for a model on this computer, Dictation for a server.
PopupErrorAction speechSetupAction(const QString &providerId);

// The popup's own captions: an error's Dismiss button, and the line shown
// while an expired sign-in is renewed before dictation starts.
QString popupDismissCaption();
QString renewingSignInText();

// The live preview line as the popups show it: text, and whether words were
// cut from its front, which the popups say by fading its start.
struct PreviewLine {
    QString text;
    bool cut = false;
};

// A live preview that overflows loses words from the front, so the newest
// words stay visible. Words are Unicode's, so CJK text is cut between words,
// not only at spaces. fits reports whether a candidate string fits the preview
// line. A single word too long to fit is cut from the front at a grapheme
// boundary.
PreviewLine trimPreviewToFit(const QString &preview, const std::function<bool(const QString &)> &fits);

} // namespace speecher

Q_DECLARE_METATYPE(speecher::PopupErrorAction)
