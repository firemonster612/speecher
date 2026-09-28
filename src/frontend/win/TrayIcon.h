#pragma once

#include <functional>
#include <memory>

#include <QObject>

namespace speecher {

class ApplicationController;
class TrayFlyout;

class TrayIcon final : public QObject {
public:
    // showSettings runs for the menu's "Settings…", showMain for a double-click.
    explicit TrayIcon(ApplicationController *controller,
                      std::function<void()> showSettings,
                      std::function<void()> showMain,
                      QObject *parent = nullptr);
    ~TrayIcon() override;

    // A balloon from the notification-area icon, which Windows 11 shows as a
    // toast. Clicking it runs clicked.
    void showMessage(const QString &title, const QString &message, std::function<void()> clicked);

    TrayFlyout *flyoutForTest();

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
