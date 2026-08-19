#pragma once

#include "app/AppFrontEnd.h"
#include "frontend/qt/QtFrontEnd.h"

#include <memory>

namespace speecher {

class ApplicationController;

// Speecher's user interface on AppKit and SwiftUI: an NSWindow whose settings
// pages come from the same schema the Qt front end renders; see
// docs/adr/0001-per-platform-front-ends.md.
//
// The dictation popup and the setup assistant are still the Qt front end's,
// which this one owns for as long as that is true.
class MacFrontEnd final : public AppFrontEnd {
public:
    explicit MacFrontEnd(ApplicationController *controller);
    ~MacFrontEnd() override;

    void showMainWindow() override;
    void showSettingsWindow() override;
    void showSetupAssistant() override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message) override;
    void alert() override;

private:
    void showWindow(const QString &pageId);

    // The Objective-C objects, so this header stays includable from C++.
    struct Native;

    ApplicationController *m_controller;
    QtFrontEnd m_qt;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
