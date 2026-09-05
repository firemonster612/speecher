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

    // Saves a picture of the window for --grab. SPEECHER_GRAB_PAGE names a
    // pane id (general, dictation, shortcut, text, delivery, apps, vocabulary,
    // accounts, whatsNew), optionally with a SelectorBar index ("vocabulary:1"),
    // to show before the grab.
    bool capture(const QString &path);

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
