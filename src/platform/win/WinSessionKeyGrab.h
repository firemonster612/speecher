#pragma once

#include "platform/SessionKeyGrab.h"

#include <QAbstractNativeEventFilter>

#include <array>

namespace speecher {

// Each key as a RegisterHotKey hot key with no modifier, which Windows
// consumes system-wide while it is registered.
class WinSessionKeyGrab final : public SessionKeyGrab, public QAbstractNativeEventFilter {
public:
    explicit WinSessionKeyGrab(QObject *parent = nullptr);
    ~WinSessionKeyGrab() override;

    bool setGrabbed(Key key, bool grabbed) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    // By Key.
    std::array<bool, 2> m_held{};
};

} // namespace speecher
