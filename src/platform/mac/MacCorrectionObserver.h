#pragma once

#include "platform/CorrectionDiff.h"

#include <QTimer>

namespace speecher::mac {

// Watches the control Speecher just wrote into and learns from the user's edit
// to it. macOS publishes value changes as accessibility notifications, so this
// waits to be told rather than polling the way the AT-SPI observer has to; the
// shared CorrectionTracker still decides what a reading means and the settle
// delay is the same, so both platforms learn from the same edits.
class CorrectionObserver {
public:
    CorrectionObserver();
    ~CorrectionObserver();
    CorrectionObserver(const CorrectionObserver &) = delete;
    CorrectionObserver &operator=(const CorrectionObserver &) = delete;

    void setEnabled(bool enabled);
    void cancel();
    // element is the AXUIElementRef the text was inserted into, kept opaque so
    // this header stays plain C++; it is retained for as long as it is watched.
    void observe(void *element,
                 qint64 processId,
                 CorrectionWindow window,
                 CorrectionTracker::Observed observed);

    // Entry points for the AX notification callback, which is a free function.
    void valueChanged();
    void elementDestroyed();

private:
    void sample();
    void stop();

    CorrectionTracker m_tracker;
    // Restarted by every value change: the edit is only learned once the text
    // has stopped moving for correctionSettleMs.
    QTimer m_settle;
    // Hard deadline, and the only place the accessibility plumbing is torn down
    // after an observation that ran to its end.
    QTimer m_deadline;
    void *m_element = nullptr;  // retained AXUIElementRef
    void *m_observer = nullptr; // AXObserverRef, released in stop()
};

} // namespace speecher::mac
