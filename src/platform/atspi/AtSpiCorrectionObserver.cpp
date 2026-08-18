#include "platform/atspi/AtSpiCorrectionObserver.h"

#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QTimer>

#include <utility>

namespace speecher::atspi {

void CorrectionObserver::setEnabled(bool enabled)
{
    m_tracker.setEnabled(enabled);
}

void CorrectionObserver::cancel()
{
    m_tracker.cancel();
}

void CorrectionObserver::schedule(
    QObject *context,
    const TargetSnapshot *snapshot,
    CorrectionWindow window,
    CorrectionTracker::Observed observed)
{
    const CorrectionWindow sampledWindow = window;
    const quint64 generation = ++m_generation;
    m_tracker.begin(std::move(window), std::move(observed));
    if (!m_tracker.active()) {
        return;
    }
    for (const int delay : {correctionFirstSampleMs,
                            correctionFirstSampleMs + correctionSettleMs,
                            correctionWindowMs}) {
        QTimer::singleShot(delay, context, [this, snapshot, sampledWindow, generation] {
            // Reading the window costs an AT-SPI round trip, so skip it once
            // this observation is over or a later one has replaced it.
            if (generation != m_generation || !snapshot || !m_tracker.active()) {
                return;
            }
            m_tracker.sample(snapshot->correctionWindow(sampledWindow));
        });
    }
}

} // namespace speecher::atspi
