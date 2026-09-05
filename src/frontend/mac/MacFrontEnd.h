#pragma once

#include "app/AppFrontEnd.h"

#include <memory>

namespace speecher {

class ApplicationController;

// Speecher's user interface on AppKit and SwiftUI: a menu bar extra, a settings
// window whose panes come from the same schema the Qt front end renders, a
// setup assistant, and a floating dictation panel. See
// docs/adr/0001-per-platform-front-ends.md and .scratch/macos-port/mac-ia.md.
class MacFrontEnd final : public AppFrontEnd {
public:
    explicit MacFrontEnd(ApplicationController *controller);
    ~MacFrontEnd() override;

    void showMainWindow() override;
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message) override;
    void alert() override;

private:
    // The Objective-C objects, so this header stays includable from C++.
    struct Native;

    ApplicationController *m_controller;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
