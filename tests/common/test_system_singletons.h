#pragma once

#include "app/ApplicationController.h"
#include "app/SpeecherRemoval.h"
#include "app/VirtualKeyboardSetup.h"
#include "frontend/quick/host/QuickPhone.h"
#include "frontend/quick/host/QuickRecording.h"
#include "frontend/quick/host/QuickRemoval.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickSystem.h"

#include <optional>
#include <utility>

namespace speecher::test {

// System, Removal and Phone over one virtual keyboard setup, with the
// Settings System reads, as the host makes them, and Recording, which Home
// reads, for tests whose pages read them.
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
        : settings(controller)
        , virtualKeyboard(controller->settings(), std::move(keyboard))
        , removal(&virtualKeyboard, std::move(removalSteps))
        , system(controller, &settings, &virtualKeyboard, &removal)
        , phone(controller)
        , recording(controller)
    {
    }

    QuickSettings settings;
    VirtualKeyboardSetup virtualKeyboard;
    QuickRemoval removal;
    QuickSystem system;
    QuickPhone phone;
    QuickRecording recording;
};

} // namespace speecher::test
