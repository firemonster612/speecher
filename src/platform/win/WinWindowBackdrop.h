#pragma once

#include "platform/WindowBackdrop.h"

class QWinEventNotifier;

namespace speecher {

// Windows 11's Mica behind a window, through DWM's system backdrop. DWM
// draws it under the whole window, so the UI keeps opaque what should not
// show it, and leaves the window's shadow and corners to DWM. Unavailable
// before Windows 11 22H2 and while Transparency effects are off, which it
// follows.
class WinWindowBackdrop final : public WindowBackdrop {
    Q_OBJECT

public:
    explicit WinWindowBackdrop(QObject *parent = nullptr);
    ~WinWindowBackdrop() override;

    bool available() const override;
    void set(QWindow *window, const QRect &area, int radius, bool dark) override;

private:
    // Asks for the next change to the Personalize key.
    void watchPersonalize();

    // The Personalize key and the event its changes signal, as HKEY and
    // HANDLE; null where the key can't be opened.
    void *m_personalize = nullptr;
    void *m_changed = nullptr;
    QWinEventNotifier *m_notifier = nullptr;
    bool m_available = false;
};

} // namespace speecher
