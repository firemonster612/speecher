#pragma once

#include <functional>
#include <memory>

#include <QString>

namespace speecher {

class ApplicationController;

// W2 replaces this stub with the settings renderer. W4 depends only on this
// small window contract so the two branches can be reconciled without pulling
// the Windows settings implementation into the front door.
class SettingsWindow {
public:
    explicit SettingsWindow(ApplicationController *controller);
    ~SettingsWindow();

    void show();
    void navigate(const QString &paneId);
    bool capture(const QString &path);
    void setActionHandler(std::function<void(QString)> handler);

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
