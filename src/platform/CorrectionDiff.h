#pragma once

#include "core/Target.h"

#include <QString>

#include <functional>

namespace speecher {

// The text Speecher inserted, pinned in place by the characters that surrounded
// it when it landed. Re-finding prefix and suffix locates the same span again
// after the user has edited it, wherever the edit has moved it to.
struct CorrectionWindow {
    Target target;
    QString original;
    QString prefix;
    QString suffix;
};

// How much surrounding text pins the window, and the least that still
// identifies it. Both target providers cut the same context, so an edit is
// learned under the same conditions whichever platform observed it.
inline constexpr int correctionContextChars = 24;
inline constexpr int correctionMinContextChars = 8;

// Observation timing, shared for the same reason. AT-SPI has no usable
// text-change signal across toolkits, so that observer polls the control at
// first, first + settle and first + 2 * settle; macOS delivers accessibility
// notifications instead, so that observer waits `settle` for the text to stop
// changing and gives up after the same total window.
inline constexpr int correctionFirstSampleMs = 2000;
inline constexpr int correctionSettleMs = 2500;
inline constexpr int correctionWindowMs = correctionFirstSampleMs + 2 * correctionSettleMs;

// Turns repeated readings of the edited control into a learned correction. An
// edit only counts once the same text has been read twice, which is what keeps
// a half-typed word out of the vocabulary; a reading that no longer locates the
// span, or that shows an edit too large to be a correction, abandons the
// observation outright rather than guessing.
class CorrectionTracker {
public:
    using Observed = std::function<void(const QString &original,
                                        const QString &corrected,
                                        const QString &applicationId,
                                        double confidence)>;

    void setEnabled(bool enabled);
    void cancel();
    void begin(CorrectionWindow window, Observed observed);
    void sample(const QString &windowText);
    bool active() const;

private:
    CorrectionWindow m_window;
    Observed m_observed;
    QString m_lastEdited;
    int m_matchingSamples = 0;
    bool m_enabled = true;
    bool m_active = false;
};

} // namespace speecher
