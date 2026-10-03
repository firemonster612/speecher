#pragma once

#include "platform/CancelKeyGrab.h"

#include <QAbstractNativeEventFilter>

namespace speecher {

// Escape as a RegisterHotKey hot key with no modifier, which Windows consumes
// system-wide while it is registered.
class WinCancelKeyGrab final : public CancelKeyGrab, public QAbstractNativeEventFilter {
public:
    explicit WinCancelKeyGrab(QObject *parent = nullptr);
    ~WinCancelKeyGrab() override;

    void setGrabbed(bool grabbed) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    bool m_grabbed = false;
};

} // namespace speecher
