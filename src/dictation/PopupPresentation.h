#pragma once

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
};

// Error text wraps at this width (logical pixels, points or DIPs) and the
// capsule grows taller rather than wider.
inline constexpr int kPopupErrorWrapWidth = 520;

// An error dismisses itself after this long, with a countdown bar beneath it.
inline constexpr int kPopupErrorDismissMs = 5000;

// A live preview that overflows loses words from the front, so the newest
// words stay visible, and "… " says something came before them. fits reports
// whether a candidate string fits the preview line. A single word too long to
// fit is cut from the front at a grapheme boundary.
QString trimPreviewToFit(const QString &preview, const std::function<bool(const QString &)> &fits);

} // namespace speecher
