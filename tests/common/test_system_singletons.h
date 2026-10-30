#pragma once

#include "app/ApplicationController.h"
#include "app/SpeecherRemoval.h"
#include "app/VirtualKeyboardSetup.h"
#include "frontend/quick/host/QuickPhone.h"
#include "frontend/quick/host/QuickRemoval.h"
#include "frontend/quick/host/QuickSystem.h"

#include <optional>
#include <utility>

namespace speecher::test {

// System, Removal and Phone over one virtual keyboard setup, as the host
// makes them, for tests whose pages read them.
struct SystemSingletons {
    // This computer's virtual keyboard and removal steps, which run only
    // when a test asks.
    explicit SystemSingletons(ApplicationController *controller)
        : SystemSingletons(controller, VirtualKeyboardSetup::onThisComputer(),
                           SpeecherRemoval::onThisComputer(controller))
    {
    }

    SystemSingletons(ApplicationController *controller, VirtualKeyboardSetup::System keyboard,
                     std::optional<SpeecherRemoval::Steps> removalSteps)
        : virtualKeyboard(controller->settings(), std::move(keyboard))
        , removal(&virtualKeyboard, std::move(removalSteps))
        , system(controller, &virtualKeyboard, &removal)
        , phone(controller)
    {
    }

    VirtualKeyboardSetup virtualKeyboard;
    QuickRemoval removal;
    QuickSystem system;
    QuickPhone phone;
};

} // namespace speecher::test
