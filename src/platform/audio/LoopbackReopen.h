#pragma once

#include <QtGlobal>

#include <optional>

namespace speecher {

// Whether a loopback capture that lost its output keeps trying to open the
// default one. An output can refuse for a while, when an app holds it
// exclusively, a format change is settling or a headset is switching
// profiles, so capture retries on later polls and gives up only after ten
// seconds of refusals. While there is no output at all it waits for one.
class LoopbackReopen {
public:
    enum class Refusal { NoOutput, Failed };

    // How long refusals go on before capture gives up.
    static constexpr qint64 kAllowanceMs = 10'000;

    // Starts the allowance afresh, for an output that closed or a new default
    // output.
    void restart();
    // Whether capture tries again on a later poll after an attempt refused at
    // elapsedMs.
    bool retries(Refusal refusal, qint64 elapsedMs);

private:
    std::optional<qint64> m_refusedSinceMs;
};

} // namespace speecher
