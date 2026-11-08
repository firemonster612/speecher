#pragma once

#include "core/DeliverySteps.h"

#include <QString>

#include <functional>

namespace speecher {

class WinPasteDelivery {
public:
    static void waitForReleasedKeys();
    // clearToInject is checked immediately before SendInput; false aborts
    // without sending anything. inputSent says whether any key went out,
    // which a failure can still have done when Windows took only part of
    // the keystroke.
    bool paste(PasteKeyCombo keys,
               const std::function<bool()> &clearToInject = {},
               QString *error = nullptr,
               bool *inputSent = nullptr);
};

} // namespace speecher
