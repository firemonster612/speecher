#pragma once

#include <QString>

namespace speecher {

// Synthesises keystrokes with CGEvent. macOS drops posted events silently when
// the process is not trusted for Accessibility, so every entry point refuses up
// front rather than letting delivery report an "Input sent" receipt it cannot
// back up.
class MacPasteDelivery {
public:
    static bool isAvailable();
    bool paste(QString *error = nullptr);
};

} // namespace speecher
