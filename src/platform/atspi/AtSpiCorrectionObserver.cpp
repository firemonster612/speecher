#include "platform/atspi/AtSpiCorrectionObserver.h"

#include "core/LearnedCorrection.h"
#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QTimer>

namespace speecher::atspi {

void CorrectionObserver::cancel()
{
    ++m_generation;
}

void CorrectionObserver::schedule(
    QObject *context,
    const TargetSnapshot *snapshot,
    CorrectionWindow window,
    std::function<void(const QString &, const QString &, const QString &, double)> observed)
{
    const quint64 generation = ++m_generation;
    QTimer::singleShot(6500, context, [this, snapshot, window = std::move(window),
                                      generation, observed = std::move(observed)] {
        const auto &[target, original, prefix, suffix] = window;
        if (generation != m_generation || !snapshot || target.secure || original.isEmpty()) return;
        const QString text = snapshot->correctionWindow(window);
        if (text.isEmpty()) return;
        const std::optional<QString> corrected = correctionBetweenAnchors(text, prefix, suffix, original);
        if (corrected) observed(original, *corrected, target.applicationId, 0.92);
    });
}

} // namespace speecher::atspi
