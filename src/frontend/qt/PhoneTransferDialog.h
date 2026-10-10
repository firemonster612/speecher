#pragma once

#include "core/AppSettings.h"

class QWidget;

namespace speecher {

// Opens "Copy settings to your phone" over parent: the QR code the Android app
// scans, what moves and what stays. The transfer listens until it closes.
void openPhoneTransferDialog(const AppSettings &settings, QWidget *parent);

} // namespace speecher
