#pragma once

#include "app/AppFrontEnd.h"

#include <memory>

namespace speecher {

class ApplicationController;

class WinFrontEnd final : public AppFrontEnd {
public:
    explicit WinFrontEnd(ApplicationController *controller);
    ~WinFrontEnd() override;

    void showMainWindow() override;
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message) override;
    void alert() override;

private:
    struct Native;

    ApplicationController *m_controller;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
