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
// first and every `settle` after it until the window closes; macOS and Windows
// deliver change notifications instead, so those observers wait `settle` for
// the text to stop changing and give up after the same window. The window is
// long enough to place the cursor in a word and retype it.
inline constexpr int correctionFirstSampleMs = 2000;
inline constexpr int correctionSettleMs = 2500;
inline constexpr int correctionWindowMs = 30000;

// Turns repeated readings of the edited control into a learned correction. An
// edit only counts once the same text has been read twice, which is what keeps
// a half-typed word out of the vocabulary. A reading that shows no correction,
// such as a word caught mid-edit, starts the count again; one that no longer
// locates the span abandons the observation outright rather than guessing.
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
