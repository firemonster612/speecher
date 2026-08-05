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
    const Target &target,
    const QString &original,
    const QString &prefix,
    const QString &suffix,
    std::function<void(const QString &, const QString &, const QString &, double)> observed)
{
    const quint64 generation = ++m_generation;
    QTimer::singleShot(6500, context, [this, snapshot, target, original, prefix, suffix,
                                      generation, observed = std::move(observed)] {
        if (generation != m_generation || !snapshot || target.secure || original.isEmpty()) return;
        const QString window = snapshot->correctionWindow(target, original, prefix, suffix);
        if (window.isEmpty()) return;
        const std::optional<QString> corrected = correctionBetweenAnchors(window, prefix, suffix, original);
        if (corrected) observed(original, *corrected, target.applicationId, 0.92);
    });
}

} // namespace speecher::atspi
