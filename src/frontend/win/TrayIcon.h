#pragma once

#include <functional>
#include <memory>

#include <QObject>

namespace speecher {

class ApplicationController;

class TrayIcon final : public QObject {
public:
    explicit TrayIcon(ApplicationController *controller,
                      std::function<void()> showSettings,
                      QObject *parent = nullptr);
    ~TrayIcon() override;

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
