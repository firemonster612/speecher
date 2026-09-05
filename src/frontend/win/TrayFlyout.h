#pragma once

#include <memory>

#include <QObject>

struct tagRECT;

namespace speecher {

class ApplicationController;

class TrayFlyout final : public QObject {
public:
    explicit TrayFlyout(ApplicationController *controller, QObject *parent = nullptr);
    ~TrayFlyout() override;

    void show(const tagRECT &iconRect);
    void hide();

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
