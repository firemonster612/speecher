#include "platform/audio/LoopbackReopen.h"

namespace speecher {
namespace {

constexpr qint64 kRefusalAllowanceMs = 10'000;

} // namespace

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
    return elapsedMs - *m_refusedSinceMs < kRefusalAllowanceMs;
}

} // namespace speecher
