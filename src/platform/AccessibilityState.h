#pragma once

namespace speecher {

struct AccessibilityState {
    bool supported = false;
    bool enabled = false;
    bool persistent = false;
};

} // namespace speecher
