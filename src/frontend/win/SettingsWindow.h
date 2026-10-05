#pragma once

#include <QString>
#include <QStringList>

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

    // Creates the window on Home if none is open, reloading the draft, or
    // brings the open one forward on its current page.
    void show();
    // show(), on this page id ("general", "vocabulary:corrections"); an
    // unknown id shows Home.
    void showPage(const QString &pageId);
    void showWhatsNew();
    // Closes the window, as its close button does.
    void close();
    bool isVisible() const;
    // For the Windows microphone privacy page opened from the dictation popup:
    // back from it, the Input device row asks again, as after its own button.
    void recheckMicrophonesOnReturn();

    // Saves a picture of the window for --grab. SPEECHER_GRAB_PAGE names the
    // page id to show before the grab, as on every front end.
    bool capture(const QString &path);

    // Asks in a ContentDialog over the window, Cancel being the default, and
    // runs `confirmed` only if the person chose confirmLabel.
    void confirm(const QString &title,
                 const QString &text,
                 const QString &confirmLabel,
                 std::function<void()> confirmed);
    // Tells the person something in a ContentDialog: what happened as its
    // title, what to do as its body, and a Close button.
    void inform(const QString &title, const QString &text);

    // What Action rows run. The window handles whatsNew itself and forwards
    // everything (whatsNew included) here; W4's front end wires the rest.
    void setActionHook(std::function<void(const QString &id)> hook);
    // Runs after this window applies a theme change, so other windows can.
    void setThemeHook(std::function<void()> hook);

private:
    friend class ::speecher::WinFrontEndTests;
    static bool offersWhatsNew(const QString &currentPane, const QString &pendingVersion);
    // What the search box suggests for query: each suggestion's "pane\nrow"
    // target, or its text when it has none.
    QStringList searchSuggestionsForTest(const QString &query);
    // The page on screen: a subpage's id, or else its pane's.
    QString shownPageForTest() const;
    // The sidebar's selected pane.
    QString selectedPaneForTest() const;
    bool backVisibleForTest() const;
    void goBackForTest();
    // Presses the index-th button on the page with this accessible name;
    // false when there is none or it is disabled.
    bool pressForTest(const QString &name, int index = 0);
    // Chooses the item labelled choice in the page's combo with this
    // accessible name.
    bool chooseForTest(const QString &name, const QString &choice);
    // The page's rating bars, in order, as "<measure> <value>"; other bars,
    // such as the microphone's level, are left out.
    QStringList ratingBarsForTest() const;
    // Opens the page's disclosure with this accessible name; false when there
    // is none.
    bool expandForTest(const QString &name);
    bool expandedForTest(const QString &name) const;
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace win
} // namespace speecher
