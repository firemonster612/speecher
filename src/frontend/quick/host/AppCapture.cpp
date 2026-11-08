#include "frontend/quick/host/AppCapture.h"

#include "dictation/DictationPorts.h"

#include <algorithm>
#include <utility>

namespace speecher {

AppCapture::AppCapture(TargetProvider *provider, std::function<bool()> busy, std::function<bool()> inFront,
                       AppCaptureTiming timing, QObject *parent)
    : QObject(parent)
    , m_provider(provider)
    , m_busy(std::move(busy))
    , m_inFront(std::move(inFront))
    , m_timing(timing)
{
    m_tick.setInterval(m_timing.tickMs);
    connect(&m_tick, &QTimer::timeout, this, &AppCapture::tick);
}

void AppCapture::start(int seconds)
{
    m_countdown = std::max(0, seconds);
    m_waiting = true;
    emit changed();
    m_tick.start();
}

void AppCapture::cancel()
{
    m_tick.stop();
    m_countdown = 0;
    m_waiting = false;
    emit changed();
}

void AppCapture::tick()
{
    if (m_countdown > 0) {
        --m_countdown;
        emit changed();
        return;
    }
    // Past the countdown, as long as Speecher is still in front.
    if (!m_inFront()) {
        m_tick.stop();
        QTimer::singleShot(m_timing.settleMs, this, &AppCapture::captureFront);
    } else if (--m_countdown < -m_timing.waitTicks) {
        m_tick.stop();
        finish();
        emit failed();
    }
}

void AppCapture::captureFront()
{
    // Capturing drops what a dictation under way captured.
    if (!m_waiting || !m_provider || m_busy()) {
        finish();
        emit failed();
        return;
    }
    m_provider->capture(this, [this](const Target &target) {
        if (!m_waiting) {
            return;
        }
        finish();
        emit captured(target);
    });
}

void AppCapture::finish()
{
    if (std::exchange(m_waiting, false)) {
        emit changed();
    }
}

} // namespace speecher
