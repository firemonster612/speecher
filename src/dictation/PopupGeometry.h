#pragma once

#include <QtGlobal>

// The dictation popup's measurements, shared by the Linux, macOS and Windows
// popups so the three draw the same capsule. Logical pixels, points or DIPs,
// as kPopupErrorWrapWidth. The error capsule and the banners size themselves.
namespace speecher::popup {

// The capsule while there are no words: a stadium this tall, holding the
// buttons and the dots.
inline constexpr int kPillHeight = 32;

// Pause (a spinner while transcribing and refining) on the left, cancel on
// the right: circles this wide around an icon this size.
inline constexpr int kButtonSize = 24;
inline constexpr int kButtonIconSize = 12;
// From a button to the dots or the status label beside it.
inline constexpr int kButtonGap = 8;
// From the outermost button, dots or label to the lobe's rounded end.
inline constexpr int kLobeAir = 6;

// The popup's waveform: fewer and finer dots than Home's row.
inline constexpr int kBarCount = 9;
inline constexpr qreal kBarWidth = 2.0;
inline constexpr qreal kBarGap = 2.0;
inline constexpr qreal kBarDotHeight = 2.0;
// The strip under the preview line: the dots at full shout (the resting dot
// times waveform::audioGain times the 1.5 wave crest) plus a pixel.
inline constexpr int kCompactStripHeight = 16;

// Room around the preview line and the strip under it.
inline constexpr int kPreviewTopMargin = 8;
inline constexpr int kPreviewSideMargin = 16;
inline constexpr int kPreviewBottomMargin = 5;
inline constexpr int kPreviewStripSpacing = 5;
// The shoulder sits as far below the text as the capsule's top sits above it,
// so the text bar reads evenly padded.
inline constexpr int kShoulderDrop = kPreviewTopMargin;
// The concave turn from the shoulder into the lobe, and the lobe's bottom
// corners.
inline constexpr qreal kFillet = 8.0;
inline constexpr qreal kLobeRadius = 14.0;

// The preview line is at most this wide, in a font this fraction of the
// platform's own, and words cut from its front fade out over this width.
inline constexpr int kMaxPreviewWidth = 320;
inline constexpr qreal kPreviewFontScale = 0.9;
inline constexpr int kPreviewFadeWidth = 28;

// The narrowest text bar that still carves the contour around a lobe this
// wide: an end cap and a whole fillet either side of the lobe. A shorter
// preview is centred in this width instead of collapsing the capsule into a
// plain rounded box.
constexpr qreal minimumPreviewBarWidth(qreal lobeWidth, qreal shoulderHeight)
{
    return lobeWidth + 2.0 * (shoulderHeight / 2.0 + kFillet);
}

} // namespace speecher::popup
