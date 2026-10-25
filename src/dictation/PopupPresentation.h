#pragma once

#include "dictation/DictationTypes.h"

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
    // A fallback did the work: another provider stood in for an unavailable
    // one, or refinement failed and the raw transcript was delivered.
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
// Where a speech service that cannot start is set up: Accounts for a
// sign-in, Local models for a model on this computer, Dictation for a server.
PopupErrorAction speechSetupAction(const QString &providerId);

// What the popup says once a Dictation Session delivered: the receipt
// ("Input sent"), then a note on the fallbacks that did the work and why, in
// one line. Exactly the receipt and speech warning of a session without a
// fallback. The fix opens Accounts when a sign-in was turned down.
struct DictationOutcome {
    QString message;
    PopupOutcome outcome = PopupOutcome::Inserted;
    PopupErrorAction fix;
};
DictationOutcome dictationOutcome(const QString &receipt,
                                  bool copiedOnly,
                                  const ProviderHistory &history,
                                  bool usedRawTranscript,
                                  const QString &speechWarning,
                                  const ProviderLabels &labels);
// The error when no speech provider in the chain could take the dictation,
// naming each and why.
QString noSpeechServiceText(const QList<ProviderAttemptIssue> &issues, const ProviderLabels &labels);

} // namespace speecher

Q_DECLARE_METATYPE(speecher::PopupErrorAction)
