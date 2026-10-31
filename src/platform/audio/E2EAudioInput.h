#pragma once

#ifdef SPEECHER_E2E_HOOKS

#include "dictation/DictationPorts.h"

#include <QTimer>

namespace speecher {

// E2E-build-only stub: alternating levels so the waveform moves, and
// silent chunks so the session believes audio is flowing.
class E2EAudioInput final : public AudioInput {
public:
    explicit E2EAudioInput(QObject *parent)
        : AudioInput(parent)
    {
        m_timer.setInterval(100);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            emit audioChunk(QByteArray(3200, '\0'));
            emit levelChanged(m_highLevel ? 0.65f : 0.12f);
            m_highLevel = !m_highLevel;
        });
    }

    void start() override
    {
        abort();
        m_active = true;
        m_timer.start();
        reportLater([this] { emit started(); });
    }

    void stop() override
    {
        if (!m_active) {
            return;
        }
        abort();
        reportLater([this] { emit stopped(); });
    }

    void abort() override
    {
        dropReports();
        m_timer.stop();
        m_active = false;
    }

    bool isActive() const override { return m_active; }

private:
    QTimer m_timer;
    bool m_active = false;
    bool m_highLevel = false;
};

} // namespace speecher

#endif // SPEECHER_E2E_HOOKS
