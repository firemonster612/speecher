#pragma once

#include "core/DeliverySteps.h"

#include <QString>

// How older releases' paste rules, and the first Apps settings, narrowed the
// Delivery Steps, and the names they stored it by. Read only to migrate it
// into a PastingChoice (migrateApps).

namespace speecher {

enum class PasteMethod {
    // The steps as they are.
    StandardPaste,
    // The steps, pressing a terminal's paste keys.
    TerminalPaste,
    // The steps without the paste keys.
    DirectInsert,
    ClipboardOnly,
};

QString pasteMethodName(PasteMethod method);
PasteMethod pasteMethodFromName(const QString &name);

} // namespace speecher
