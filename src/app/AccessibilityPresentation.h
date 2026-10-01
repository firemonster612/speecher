#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

// How every front end words the desktop accessibility permission: the setup
// step and the notice Home and the tray panels show while it is missing.
// enabled is whether it is on now; persistent whether it stays on after the
// next login, which only Linux distinguishes.

// The button that turns it on, or keeps it on after login; empty once it is
// on for good.
QString accessibilityActionCaption(bool enabled, bool persistent);
// What the permission buys, one line each, and each one's verdict.
QStringList accessibilityCapabilities();
QString accessibilityCapabilityStatus(bool allowed);
// The setup step's status line.
QString accessibilitySetupStatus(bool supported, bool enabled, bool persistent);
// The line under the setup step's button.
QString accessibilitySetupFootnote();
// The notice while it is off or on for this session only; compact for the
// narrow tray panels.
QString accessibilityNoticeText(bool enabled, bool compact);

} // namespace speecher
