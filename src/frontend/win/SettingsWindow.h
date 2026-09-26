#pragma once

#include <QString>

#include <functional>
#include <memory>

namespace speecher {

class ApplicationController;
class WinFrontEndTests;

namespace win {

// The settings window: a Mica Microsoft.UI.Xaml.Window with the TitleBar
// control, a left NavigationView over the pane table, search, the update
// banner, and the What's New page — the Windows 11 Settings app's shell around
// the schema. Owns the SettingsModel; recreated windows reuse it.
class SettingsWindow {
public:
    explicit SettingsWindow(ApplicationController *controller);
    ~SettingsWindow();

    // Creates the window if none is open, brings it forward, reloads the
    // draft, and remembers the pane from last time.
    void show();
    void showWhatsNew();
    bool isVisible() const;

    // Saves a picture of the window for --grab. SPEECHER_GRAB_PAGE names a
    // schema page id (general, audio, refinement, output, vocabulary,
    // corrections, bindings, providers, whatsNew) or the home or shortcut
    // pane, to show before the grab. A ":index" suffix is tolerated and ignored.
    bool capture(const QString &path);

    // Asks in a ContentDialog over the window, Cancel being the default, and
    // runs `confirmed` only if the person chose confirmLabel.
    void confirm(const QString &title,
                 const QString &text,
                 const QString &confirmLabel,
                 std::function<void()> confirmed);

    // What Action rows run. The window handles whatsNew itself and forwards
    // everything (whatsNew included) here; W4's front end wires the rest.
    void setActionHook(std::function<void(const QString &id)> hook);

private:
    friend class ::speecher::WinFrontEndTests;
    static bool offersWhatsNew(const QString &currentPane, const QString &pendingVersion);
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace win
} // namespace speecher
