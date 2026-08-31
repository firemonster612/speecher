#include "platform/CorrectionDiff.h"

#include "core/LearnedCorrection.h"

#include <optional>
#include <utility>

namespace speecher {
namespace {

// A span longer than this is a paragraph the user rewrote, not a correction of
// what Speecher dictated.
constexpr int maxEditedSpanChars = 500;

std::optional<QString> editedSpan(const QString &text, const CorrectionWindow &window)
{
    if (text.isEmpty() || window.prefix.size() < correctionMinContextChars
        || window.suffix.size() < correctionMinContextChars) {
        return std::nullopt;
    }
    // Context that repeats cannot say which occurrence Speecher wrote into.
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
    return span.size() <= maxEditedSpanChars ? std::optional<QString>(span) : std::nullopt;
}

} // namespace

void CorrectionTracker::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        cancel();
    }
}

void CorrectionTracker::cancel()
{
    m_active = false;
    m_observed = {};
    m_lastEdited.clear();
    m_matchingSamples = 0;
}

void CorrectionTracker::begin(CorrectionWindow window, Observed observed)
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

void CorrectionTracker::sample(const QString &windowText)
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

bool CorrectionTracker::active() const
{
    return m_active;
}

} // namespace speecher
