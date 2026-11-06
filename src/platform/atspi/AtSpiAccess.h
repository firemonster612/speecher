#pragma once

#include "platform/AccessibilityState.h"

#include <QString>

#ifdef SPEECHER_WITH_ATSPI
#include <atspi/atspi.h>
#endif

namespace speecher {
struct DesktopIntegrationRemoval;
}

namespace speecher::atspi {

AccessibilityState accessibilityState();
// Turns on AT-SPI for this session, and on desktops mayAnnounceScreenReader()
// allows, also tells Chromium and Electron apps a screen reader is running,
// until Speecher quits.
bool requestAccessibility(QString *error = nullptr);
// Whether the desktop XDG_CURRENT_DESKTOP names is safe to tell a screen
// reader is running.
bool mayAnnounceScreenReader(const QString &currentDesktop);
bool enableAccessibilityPermanently(QString *error = nullptr);
// Undoes enableAccessibilityPermanently() for removal: deletes its
// environment.d file unless someone changed it, and records the outcome in
// removal as the "accessibility setting".
void removePermanentAccessibility(DesktopIntegrationRemoval &removal);

class AccessibleHandle {
public:
    AccessibleHandle() = default;
#ifdef SPEECHER_WITH_ATSPI
    explicit AccessibleHandle(AtspiAccessible *accessible);
    AtspiAccessible *get() const;
#endif
    ~AccessibleHandle();

    AccessibleHandle(const AccessibleHandle &) = delete;
    AccessibleHandle &operator=(const AccessibleHandle &) = delete;
    AccessibleHandle(AccessibleHandle &&other) noexcept;
    AccessibleHandle &operator=(AccessibleHandle &&other) noexcept;

    explicit operator bool() const;
    void reset();

private:
    void *m_accessible = nullptr;
};

#ifdef SPEECHER_WITH_ATSPI
void clearError(GError **error);
QString takeString(gchar *value);
bool hasState(AtspiAccessible *object, AtspiStateType state);
#endif

} // namespace speecher::atspi
