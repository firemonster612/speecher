#pragma once

#include "platform/WindowBackdrop.h"

namespace speecher {

// macOS's sidebar vibrancy behind a window: an NSVisualEffectView with
// rounded corners in a borderless child window just below it, showing the
// desktop blurred where the UI is translucent. Unavailable while Reduce
// transparency is on.
class MacWindowBackdrop final : public WindowBackdrop {
    Q_OBJECT

public:
    explicit MacWindowBackdrop(QObject *parent = nullptr);
    ~MacWindowBackdrop() override;

    bool available() const override;
    void set(QWindow *window, const QRect &area, int radius, bool dark) override;

private:
    // The observer of the accessibility display options, as an id.
    void *m_observer = nullptr;
};

} // namespace speecher
