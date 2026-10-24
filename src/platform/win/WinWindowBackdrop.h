#pragma once

#include "platform/WindowBackdrop.h"

namespace speecher {

// Windows 11's Mica behind a window, through DWM's system backdrop. DWM
// draws it under the whole window, so the UI keeps opaque what should not
// show it. Unavailable before Windows 11 22H2 and while Transparency
// effects are off.
class WinWindowBackdrop final : public WindowBackdrop {
    Q_OBJECT

public:
    using WindowBackdrop::WindowBackdrop;

    bool available() const override;
    void set(QWindow *window, const QRect &area, int radius, bool dark) override;
};

} // namespace speecher
