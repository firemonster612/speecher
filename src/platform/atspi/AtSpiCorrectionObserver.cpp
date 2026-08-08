#include "platform/atspi/AtSpiCorrectionObserver.h"

#include "core/LearnedCorrection.h"
#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QTimer>

namespace speecher::atspi {
namespace {

std::optional<QString> editedSpan(const QString &text, const CorrectionWindow &window)
{
    if (text.isEmpty() || window.prefix.size() < 8 || window.suffix.size() < 8) {
        return std::nullopt;
    }
    const int prefixAt = text.indexOf(window.prefix);
    if (prefixAt < 0 || text.indexOf(window.prefix, prefixAt + 1) >= 0) {
        return std::nullopt;
    }
    const int start = prefixAt + window.prefix.size();
    const int suffixAt = text.indexOf(window.suffix, start);
    if (suffixAt < start || text.indexOf(window.suffix, suffixAt + 1) >= 0) {
        return std::nullopt;
    }
    const QString span = text.mid(start, suffixAt - start);
    return span.size() <= 500 ? std::optional<QString>(span) : std::nullopt;
}

} // namespace

void CorrectionObserver::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        cancel();
    }
}

void CorrectionObserver::cancel()
{
    ++m_generation;
    m_active = false;
    m_observed = {};
    m_lastEdited.clear();
    m_matchingSamples = 0;
}

void CorrectionObserver::begin(CorrectionWindow window, Observed observed)
{
    cancel();
    if (!m_enabled || window.target.secure || window.target.hasSelection()
        || window.original.isEmpty() || !observed) {
        return;
    }
    m_window = std::move(window);
    m_observed = std::move(observed);
    m_active = true;
}

void CorrectionObserver::sample(const QString &windowText)
{
    if (!m_active) {
        return;
    }
    const std::optional<QString> edited = editedSpan(windowText, m_window);
    if (!edited) {
        cancel();
        return;
    }
    if (*edited == m_window.original) {
        m_lastEdited.clear();
        m_matchingSamples = 0;
        return;
    }
    const std::optional<CorrectionEvidence> evidence = analyzeCorrection(m_window.original, *edited);
    if (!evidence) {
        cancel();
        return;
    }
    if (*edited == m_lastEdited) {
        ++m_matchingSamples;
    } else {
        m_lastEdited = *edited;
        m_matchingSamples = 1;
    }
    if (m_matchingSamples < 2) {
        return;
    }

    const Target target = m_window.target;
    const Observed observed = m_observed;
    cancel();
    observed(evidence->original, evidence->corrected,
             target.applicationId, evidence->confidence);
}

void CorrectionObserver::schedule(
    QObject *context,
    const TargetSnapshot *snapshot,
    CorrectionWindow window,
    Observed observed)
{
    const CorrectionWindow sampledWindow = window;
    begin(std::move(window), std::move(observed));
    if (!m_active) {
        return;
    }
    const quint64 generation = m_generation;
    for (const int delay : {2000, 4500, 7000}) {
        QTimer::singleShot(delay, context, [this, snapshot, sampledWindow, generation] {
            if (generation != m_generation || !snapshot) {
                return;
            }
            sample(snapshot->correctionWindow(sampledWindow));
        });
    }
}

} // namespace speecher::atspi
