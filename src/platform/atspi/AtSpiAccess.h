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
bool requestAccessibility(QString *error = nullptr);
// Keeps AT-SPI on across sign-outs through an environment.d file, and has
// Chromium and Electron apps build their accessibility tree through
// writeAccessibleLaunchers(); then turns AT-SPI on for this session.
bool enableAccessibilityPermanently(QString *error = nullptr);
// Where Speecher's environment.d file is there, writes the Chromium app
// launchers again, so they follow apps updated, installed or removed since.
void refreshPermanentAccessibility();
// Whether a file enableAccessibilityPermanently() wrote is there, unchanged,
// for disablePermanentAccessibility() to delete.
bool canDisablePermanentAccessibility();
// Undoes enableAccessibilityPermanently() as removePermanentAccessibility()
// does, leaving AT-SPI on for this session. The error names what stayed.
bool disablePermanentAccessibility(QString *error = nullptr);
// Undoes enableAccessibilityPermanently() for removal: deletes its
// environment.d file and Chromium app launchers unless someone changed them,
// and records the outcome in removal as the "accessibility setting" and the
// "Chromium app launchers".
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
