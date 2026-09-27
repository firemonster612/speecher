#pragma once

#include <QString>

#include <functional>
#include <memory>

namespace speecher {

class ApplicationController;
class WinFrontEndTests;

namespace win {

class TranscribePane;

// The settings window: a Mica Microsoft.UI.Xaml.Window with the TitleBar
// control, a left NavigationView over the pane table, search, the update
// banner, and the What's New page — the Windows 11 Settings app's shell around
// the schema. Owns the SettingsModel; recreated windows reuse it.
class SettingsWindow {
public:
    // transcribe backs the Transcribe pane and outlives the window.
    SettingsWindow(ApplicationController *controller, TranscribePane *transcribe);
    ~SettingsWindow();

    // Creates the window if none is open, brings it forward, reloads the
    // draft, and remembers the pane from last time.
    void show();
    // show(), on this page id ("general", "vocabulary:corrections"); an
    // unknown id shows Home.
    void showPage(const QString &pageId);
    void showWhatsNew();
    // Closes the window, as its close button does.
    void close();
    bool isVisible() const;

    // Saves a picture of the window for --grab. SPEECHER_GRAB_PAGE names the
    // page id to show before the grab, as on every front end.
    bool capture(const QString &path);

    // Asks in a ContentDialog over the window, Cancel being the default, and
    // runs `confirmed` only if the person chose confirmLabel.
    void confirm(const QString &title,
                 const QString &text,
                 const QString &confirmLabel,
                 std::function<void()> confirmed);
    // Tells the person something in a ContentDialog with an OK button.
    void inform(const QString &title);

    // What Action rows run. The window handles whatsNew itself and forwards
    // everything (whatsNew included) here; W4's front end wires the rest.
    void setActionHook(std::function<void(const QString &id)> hook);
    // Runs after this window applies a theme change, so other windows can.
    void setThemeHook(std::function<void()> hook);

private:
    friend class ::speecher::WinFrontEndTests;
    static bool offersWhatsNew(const QString &currentPane, const QString &pendingVersion);
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace win
} // namespace speecher
