#pragma once

#include "core/InsightsSummary.h"
#include "core/ShortcutBinding.h"
#include "frontend/win/SettingsModel.h"

#include <QHash>

#include <windows.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <functional>
#include <memory>

namespace speecher {

class ApplicationController;
class MicrophoneTest;

namespace win {

class CollectionEditor;
class LocalModelBrowser;
class WaveformBars;
class SettingsModel;

// The widest of the three control widths a settings row uses, for text that
// needs room: a paragraph, a URL, a key.
inline constexpr double kWideControlWidth = 320;

// The Gallery's settings page column: its widest, and the gutter either side
// of it. Whatever lines up with the cards uses both.
inline constexpr double kPageColumnWidth = 1064;
inline constexpr double kPageGutter = 36;

inline winrt::hstring hs(const QString &text)
{
    return winrt::hstring(reinterpret_cast<const wchar_t *>(text.utf16()),
                          static_cast<uint32_t>(text.size()));
}

inline QString qs(const winrt::hstring &text)
{
    return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

inline winrt::Microsoft::UI::Xaml::ElementTheme requestedTheme(const QString &setting)
{
    if (setting == QStringLiteral("light")) {
        return winrt::Microsoft::UI::Xaml::ElementTheme::Light;
    }
    if (setting == QStringLiteral("dark")) {
        return winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
    }
    return winrt::Microsoft::UI::Xaml::ElementTheme::Default;
}

// The services rows need from the window, plus the state that must survive a
// pane rebuild. One of these lives on each window that shows panes: the
// settings window and the Transcribe window.
struct PaneHost {
    SettingsModel *model = nullptr;
    ApplicationController *controller = nullptr;
    // The window's lifetime token: XAML handlers that reference this
    // host hold a weak copy and bail once the window's Native is gone — a
    // member-null check cannot establish object lifetime.
    std::shared_ptr<bool> alive;
    // Queued rebuild of the visible pane, after a write re-derived the rows.
    std::function<void()> refresh;
    // Action rows (runSetup, checkForUpdates, whatsNew) and disabledAction ids.
    std::function<void(const QString &id)> action;
    // Shows a page by id ("general", "vocabulary:corrections"), for links on
    // a page.
    std::function<void(const QString &pageId)> showPage;
    // The view each Alternatives pane shows, by pane id.
    QHash<QString, QString> views;
    // The window's HWND, which the file picker needs.
    std::function<HWND()> hwnd;
    // The window's XamlRoot, which ContentDialog needs.
    std::function<winrt::Microsoft::UI::Xaml::XamlRoot()> xamlRoot;
    // The root's ActualTheme, which the code-resolved brushes follow: an
    // element-level ThemeResource resolves against the application theme, not
    // the window's, so brushes are picked from the theme dictionaries by hand.
    std::function<winrt::Microsoft::UI::Xaml::ElementTheme()> effectiveTheme;
    // Collection editors by row id, kept across pane rebuilds so an undo
    // history survives an unrelated setting changing.
    QHash<QString, std::shared_ptr<CollectionEditor>> editors;
    // The Local models list and detail, kept for the same reason: its
    // selection outlives the rebuild each LocalSetup change causes.
    std::shared_ptr<LocalModelBrowser> localModels;
    // Home's waveform while dictating, and the connection feeding it the
    // level; both replaced each time Home is rebuilt.
    std::shared_ptr<WaveformBars> homeWaveform;
    QMetaObject::Connection homeLevel;
    // The Test microphone row's test, kept for the same reason. Reset, which
    // closes the microphone, on a pane change and when the window closes.
    std::shared_ptr<MicrophoneTest> microphoneTest;
    // The OpenAI credential field's state: a keyring read that lands after
    // typing started must not overwrite what was typed.
    QString apiKey;
    int apiKeyEdits = 0;
    bool apiKeyLoaded = false;
    QString credentialProblem;
    // What the Global Shortcut dialog left for the Dictation row to say, and
    // which of the two shortcut rows it is about.
    QString shortcutProblem;
    // The single-key typing cost, shown inline after a save; not an error.
    QString shortcutNotice;
    GlobalShortcutRole shortcutNoteRole = GlobalShortcutRole::Dictation;
    bool shortcutRecording = false;
    // A row a search found, which the next pane build scrolls to.
    QString revealRow;
    // Home's pickers: the stats period, and the Activity measure as an index
    // into Dictations / Words / Minutes of audio.
    InsightsRange homeRange = InsightsRange::Last30Days;
    int homeMeasure = 0;
};

// Whether a deferred callback's window Native is gone. Dispatcher work and
// coroutines resumed after a picker hold a weak copy of PaneHost::alive; a
// member-null check cannot establish object lifetime.
inline bool gone(const std::weak_ptr<bool> &weak)
{
    const std::shared_ptr<bool> alive = weak.lock();
    return !alive || !*alive;
}

// Shows page in pageHost at the scroll offset of the page it replaces, so a
// rebuild does not jump back to the top; a page that is not a rebuild of the
// one before passes keepScroll false and opens at its top.
void replacePage(const winrt::Microsoft::UI::Xaml::Controls::Border &pageHost,
                 const winrt::Microsoft::UI::Xaml::UIElement &page,
                 bool keepScroll = true);

// Saves a window's client pixels for --grab through PrintWindow, which
// composes the swap chain content DWM holds; RenderTargetBitmap misses the
// backdrop.
bool printWindowTo(HWND handle, const QString &path);

// Gives a window and its TitleBar the speecher.ico beside the executable.
void setWindowIcon(const winrt::Microsoft::UI::Xaml::Window &window,
                   const winrt::Microsoft::UI::Xaml::Controls::TitleBar &titleBar);

// One pane as a WinUI page: ScrollViewer over a 1064-wide column with the
// pane title, one card per group — BodyStrong headers, SettingsCard-shaped
// rows spaced 4 and Caption footnotes — the WinUI Gallery settings page, built
// in code. An Alternatives pane shows one group at a time under a SelectorBar.
winrt::Microsoft::UI::Xaml::UIElement buildPane(const SettingsPane &pane, PaneHost &host);


// The Gallery's settings page scaffold: gutters on the scroller, the column
// capped at 1064 inside them, the page title on top (none when empty). Shared
// with Home and Transcribe.
winrt::Microsoft::UI::Xaml::Controls::ScrollViewer pageScaffold(
    const QString &title, const winrt::Microsoft::UI::Xaml::Controls::StackPanel &column);

// A pageScaffold scroller with an action that stays in view below it, at the
// column's margins and width, as a dialog keeps its buttons.
winrt::Microsoft::UI::Xaml::Controls::Grid pageWithActionBar(
    const winrt::Microsoft::UI::Xaml::Controls::ScrollViewer &scroll,
    const winrt::Microsoft::UI::Xaml::UIElement &action);

// The scroller of a page: the page itself, or a pageWithActionBar's.
winrt::Microsoft::UI::Xaml::Controls::ScrollViewer pageScroller(const winrt::Microsoft::UI::Xaml::UIElement &page);

// A SettingsCard-shaped container (Card brushes, 1 px stroke, control corner
// radius) around arbitrary content; shared with the collection editor and the
// full-width custom rows so every card on screen is the same card.
winrt::Microsoft::UI::Xaml::Controls::Border cardContainer(
    const winrt::Microsoft::UI::Xaml::UIElement &content);

// One SettingsCard row grid: label + description on the left, the control on
// the right. followsRow adds the inset top separator grouped rows share.
winrt::Microsoft::UI::Xaml::Controls::Grid rowGrid(const RowSnapshot &row,
                                                   const winrt::Microsoft::UI::Xaml::UIElement &control,
                                                   PaneHost &host,
                                                   bool followsRow);

// A grid with the inset top separator that rows following another row in the
// same card share.
winrt::Microsoft::UI::Xaml::Controls::Grid separatedGrid();

// Detaches an element from whatever parent a discarded pane left it in, so a
// cached element can be shown in a rebuilt one.
void detachFromParent(const winrt::Microsoft::UI::Xaml::UIElement &element);

// A styled TextBlock in the window's primary foreground.
winrt::Microsoft::UI::Xaml::Controls::TextBlock styledTextBlock(const QString &text,
                                                                const wchar_t *styleKey);

// A styled TextBlock in the secondary foreground of the window's ActualTheme.
// Resolved in code from the style dictionary's theme dictionaries: every
// XAML-side route (style setters, element-level ThemeResource on parsed
// elements) resolves against the application theme, which stays the system's
// while the window follows the theme setting.
winrt::Microsoft::UI::Xaml::Controls::TextBlock secondaryTextBlock(const QString &text,
                                                                   const wchar_t *styleKey,
                                                                   const PaneHost &host);

// Gives text the secondary foreground of the theme it is shown in, once it is
// in a window: secondaryTextBlock for surfaces without a PaneHost, the setup
// assistant and the tray flyout.
void followSecondaryForeground(const winrt::Microsoft::UI::Xaml::Controls::TextBlock &text);

// Opens Windows Settings at Privacy > Microphone, where a microphone Speecher
// cannot use is allowed again.
void openMicrophonePrivacySettings();

// Whether a Windows contrast theme is on, which overrides Light and Dark.
bool highContrastOn();

// A brush from styles.xaml's theme dictionary for the window's ActualTheme (or
// the contrast theme), resolved in code for the reason secondaryTextBlock
// gives. Null when the key is missing.
winrt::Microsoft::UI::Xaml::Media::Brush themeBrush(const wchar_t *key, const PaneHost &host);

// A short label on a pill of brush at the heatmap's lightest level: a Writing
// Profile on Home, a Local Model's rating. WinUI's InfoBadge holds only a
// number or an icon, so this is the text badge the Linux and macOS front ends
// draw.
winrt::Microsoft::UI::Xaml::Controls::Grid badge(const QString &label,
                                                 const winrt::Microsoft::UI::Xaml::Media::Brush &brush);

// A Choice row's control: options as items, disabled ones kept visible, the
// write going through setValueAndCommit. Shared with the pickers the custom
// rows supply options for.
winrt::Microsoft::UI::Xaml::Controls::ComboBox choiceComboBox(const RowSnapshot &row,
                                                              PaneHost &host);

// A toggle switch as the Settings app shows one: On or Off on its left, its
// right edge on the line the other controls share. The word follows the
// switch as it flips.
winrt::Microsoft::UI::Xaml::Controls::StackPanel stateToggle(
    const winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch &toggle);

// Lets a TextBox hold several lines, for a multi-line row or column: Return
// starts a new line, so the value is saved when the box loses focus.
void makeMultiline(const winrt::Microsoft::UI::Xaml::Controls::TextBox &box);

// Writes a row's value through the model, commits, and queues a pane rebuild
// so gated rows re-derive — the immediate-apply save model the mac front end
// uses.
void setValueAndCommit(PaneHost &host, const QString &rowId, const QVariant &value);

} // namespace win
} // namespace speecher
