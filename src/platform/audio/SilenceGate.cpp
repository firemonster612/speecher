#include "platform/audio/SilenceGate.h"

#include <utility>

namespace speecher {

void SilenceGate::setSizes(int preRollBytes, int postRollBytes)
{
    m_preRollBytes = preRollBytes;
    m_postRollBytes = postRollBytes;
}

void SilenceGate::holdQuiet(const QByteArray &pcm)
{
    if (m_preRollBytes <= 0) {
        m_preRoll.clear();
        return;
    }
    if (pcm.size() >= m_preRollBytes) {
        m_preRoll = pcm.right(m_preRollBytes);
        return;
    }
    m_preRoll.append(pcm);
    if (m_preRoll.size() > m_preRollBytes) {
        m_preRoll.remove(0, m_preRoll.size() - m_preRollBytes);
    }
}

QList<QByteArray> SilenceGate::pass(const QByteArray &pcm, bool voiced)
{
    QList<QByteArray> out;
    if (!m_speaking) {
        if (!voiced) {
            holdQuiet(pcm);
            return out;
        }

        m_speaking = true;
        // The buffered lead-in first, then the whole voiced chunk. Folding the
        // chunk into the size-capped pre-roll buffer before this check used to
        // truncate the start of any voiced chunk longer than the pre-roll
        // window.
        if (!m_preRoll.isEmpty()) {
            out.append(takePreRoll());
        }
        out.append(pcm);
        m_pendingPostRoll.clear();
        return out;
    }

    if (voiced) {
        if (!m_pendingPostRoll.isEmpty()) {
            out.append(std::exchange(m_pendingPostRoll, {}));
        }
        out.append(pcm);
        return out;
    }

    if (m_postRollBytes <= 0) {
        reset(false);
        return out;
    }

    m_pendingPostRoll.append(pcm);
    if (m_pendingPostRoll.size() >= m_postRollBytes) {
        // The post-roll is sent; only the quiet after it can lead in again.
        m_preRoll = m_pendingPostRoll.mid(m_postRollBytes).right(m_preRollBytes);
        out.append(flushPostRoll());
        m_speaking = false;
    }
    return out;
}

QByteArray SilenceGate::takePreRoll()
{
    return std::exchange(m_preRoll, {});
}

QByteArray SilenceGate::flushPostRoll()
{
    return std::exchange(m_pendingPostRoll, {}).left(m_postRollBytes);
}

void SilenceGate::reset(bool keepPreRoll)
{
    if (!keepPreRoll) {
        m_preRoll.clear();
    }
    m_pendingPostRoll.clear();
    m_speaking = false;
}

} // namespace speecher
