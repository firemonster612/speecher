#include "platform/audio/LoopbackReopen.h"

namespace speecher {

void LoopbackReopen::restart()
{
    m_refusedSinceMs.reset();
}

bool LoopbackReopen::retries(Refusal refusal, qint64 elapsedMs)
{
    if (refusal == Refusal::NoOutput) {
        m_refusedSinceMs.reset();
        return true;
    }
    if (!m_refusedSinceMs) {
        m_refusedSinceMs = elapsedMs;
    }
    return elapsedMs - *m_refusedSinceMs < kAllowanceMs;
}

} // namespace speecher
