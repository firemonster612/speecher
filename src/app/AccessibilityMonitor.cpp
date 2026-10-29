#include "app/AccessibilityMonitor.h"

#include "app/PlatformComposition.h"

#include <QTimer>

namespace speecher {
namespace {

#ifdef Q_OS_MACOS
constexpr int accessibilityPollMs = 5000;
#endif

} // namespace

AccessibilityMonitor::AccessibilityMonitor(const PlatformComposition &platform, QObject *parent)
    : QObject(parent)
    , m_platform(platform)
{
#ifdef Q_OS_MACOS
    m_poll = new QTimer(this);
    m_poll->setInterval(accessibilityPollMs);
    connect(m_poll, &QTimer::timeout, this, &AccessibilityMonitor::refresh);
#endif
    m_platform.watchAccessibilityChanges(this, [this] { refresh(); });
}

bool AccessibilityMonitor::supported() const
{
    return m_supported;
}

bool AccessibilityMonitor::enabled() const
{
    return m_enabled;
}

bool AccessibilityMonitor::persistent() const
{
    return m_persistent;
}

void AccessibilityMonitor::checkAtStartup()
{
    const AccessibilityState state = m_platform.accessibilityState();
    const bool requestSucceeded = state.persistent && m_platform.requestAccessibility();
    m_supported = state.supported;
    m_enabled = state.enabled || requestSucceeded;
    m_persistent = state.persistent;
#ifdef Q_OS_MACOS
    if (m_enabled) {
        m_poll->stop();
    } else {
        m_poll->start();
    }
#endif
    emit stateChanged(m_supported, m_enabled, m_persistent);
}

bool AccessibilityMonitor::enablePermanently(QString *error)
{
    const bool enabled = m_platform.enableAccessibilityPermanently(error);
    refresh();
    return enabled;
}

void AccessibilityMonitor::refresh()
{
    const AccessibilityState state = m_platform.accessibilityState();
    const bool changed = m_supported != state.supported
        || m_enabled != state.enabled
        || m_persistent != state.persistent;
    m_supported = state.supported;
    m_enabled = state.enabled;
    m_persistent = state.persistent;
#ifdef Q_OS_MACOS
    if (m_enabled) {
        m_poll->stop();
    } else if (!m_poll->isActive()) {
        m_poll->start();
    }
#endif
    if (!changed) {
        return;
    }
    emit stateChanged(m_supported, m_enabled, m_persistent);
}

} // namespace speecher
