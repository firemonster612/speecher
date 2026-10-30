#include "platform/audio/SilenceGate.h"

#include <algorithm>

namespace speecher {

void SilenceGate::holdQuiet(const QByteArray &pcm, int preRollBytes)
{
    if (preRollBytes <= 0) {
        preRoll.clear();
        return;
    }
    if (pcm.size() >= preRollBytes) {
        preRoll = pcm.right(preRollBytes);
        return;
    }
    preRoll.append(pcm);
    if (preRoll.size() > preRollBytes) {
        preRoll.remove(0, preRoll.size() - preRollBytes);
    }
}

QList<QByteArray> SilenceGate::pass(const QByteArray &pcm, bool voiced, int preRollBytes, int postRollBytes)
{
    QList<QByteArray> out;
    if (!speaking) {
        if (!voiced) {
            holdQuiet(pcm, preRollBytes);
            return out;
        }

        speaking = true;
        // The buffered lead-in first, then the whole voiced chunk. Folding the
        // chunk into the size-capped pre-roll buffer before this check used to
        // truncate the start of any voiced chunk longer than the pre-roll
        // window.
        if (!preRoll.isEmpty()) {
            out.append(preRoll);
            preRoll.clear();
        }
        out.append(pcm);
        pendingPostRoll.clear();
        return out;
    }

    if (voiced) {
        if (!pendingPostRoll.isEmpty()) {
            out.append(pendingPostRoll);
            pendingPostRoll.clear();
        }
        out.append(pcm);
        return out;
    }

    if (postRollBytes <= 0) {
        pendingPostRoll.clear();
        preRoll.clear();
        speaking = false;
        return out;
    }

    pendingPostRoll.append(pcm);
    if (pendingPostRoll.size() >= postRollBytes) {
        out.append(pendingPostRoll.left(postRollBytes));
        const int keepBytes = std::min(int(pendingPostRoll.size()), preRollBytes);
        preRoll = pendingPostRoll.right(keepBytes);
        pendingPostRoll.clear();
        speaking = false;
    }
    return out;
}

} // namespace speecher
