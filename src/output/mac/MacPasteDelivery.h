#pragma once

#include <QString>

#include <functional>

namespace speecher {

// Synthesises keystrokes with CGEvent. macOS drops posted events silently when
// the process is not trusted for Accessibility, so every entry point refuses up
// front rather than letting delivery report an "Input sent" receipt it cannot
// back up.
class MacPasteDelivery {
public:
    static bool isAvailable();
    // clearToInject is checked immediately before the keystroke is posted;
    // false aborts without sending anything.
    bool paste(const std::function<bool()> &clearToInject = {}, QString *error = nullptr);
};

} // namespace speecher
