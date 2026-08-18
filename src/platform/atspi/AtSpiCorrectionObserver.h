#pragma once

#include "platform/CorrectionDiff.h"

class QObject;

namespace speecher::atspi {

class TargetSnapshot;

// AT-SPI publishes no text-change signal Speecher can rely on across toolkits,
// so observation is a short poll of the inserted span. CorrectionTracker owns
// what the readings mean; this only decides when to take them.
class CorrectionObserver {
public:
    void setEnabled(bool enabled);
    void cancel();
    void schedule(QObject *context,
                  const TargetSnapshot *snapshot,
                  CorrectionWindow window,
                  CorrectionTracker::Observed observed);

private:
    CorrectionTracker m_tracker;
    // Identifies the current observation so a timer left over from the previous
    // one cannot sample this one's window.
    quint64 m_generation = 0;
};

} // namespace speecher::atspi
