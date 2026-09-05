#pragma once

#include <QAbstractNativeEventFilter>

#include <memory>

namespace speecher {

class WinUiHost final : public QAbstractNativeEventFilter {
public:
    WinUiHost();
    ~WinUiHost() override;

    void installNativeEventFilter();
    void shutdown();

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
