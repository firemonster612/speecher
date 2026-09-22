#pragma once

#include "core/PasteRules.h"

#include <QString>

#include <functional>

namespace speecher {

class WinPasteDelivery {
public:
    static void waitForReleasedKeys();
    // clearToInject is checked immediately before SendInput; false aborts
    // without sending anything.
    bool paste(PasteMethod method,
               const std::function<bool()> &clearToInject = {},
               QString *error = nullptr);
};

} // namespace speecher
