#pragma once

#include "frontend/win/SettingsModel.h"

#include <QHash>

#include <windows.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#pragma pop_macro("GetCurrentTime")

#include <functional>
#include <memory>

namespace speecher {

class ApplicationController;

namespace win {

class CollectionEditor;
class SettingsModel;

inline winrt::hstring hs(const QString &text)
{
    return winrt::hstring(reinterpret_cast<const wchar_t *>(text.utf16()),
                          static_cast<uint32_t>(text.size()));
}

inline QString qs(const winrt::hstring &text)
{
    return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}

// The services rows need from the window, plus the state that must survive a
// pane rebuild. One of these lives on the settings window.
struct PaneHost {
    SettingsModel *model = nullptr;
    ApplicationController *controller = nullptr;
    // Queued rebuild of the visible pane, after a write re-derived the rows.
    std::function<void()> refresh;
    // Action rows (runSetup, checkForUpdates, whatsNew) and disabledAction ids.
    std::function<void(const QString &id)> action;
    // The settings window's HWND, which the file picker needs.
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
    // The OpenAI credential field's state: a keyring read that lands after
    // typing started must not overwrite what was typed.
    QString apiKey;
    int apiKeyEdits = 0;
    bool apiKeyLoaded = false;
    QString credentialProblem;
    // Shortcut pane state.
    QString shortcutProblem;
    bool shortcutRecording = false;
};

// One schema page as a WinUI page: ScrollViewer over a 1064-wide column with
// the page title, one card per schema section — BodyStrong headers,
// SettingsCard-shaped rows spaced 4 and Caption footnotes — the WinUI Gallery
// settings page, built in code.
winrt::Microsoft::UI::Xaml::UIElement buildPage(const PageSnapshot &page, PaneHost &host);

// The global-shortcut recorder page, the one settings surface with no schema
// page behind it.
winrt::Microsoft::UI::Xaml::UIElement buildShortcutPage(PaneHost &host);

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

// A Choice row's control: options as items, disabled ones kept visible, the
// write going through setValueAndCommit. Shared with the pickers the custom
// rows supply options for.
winrt::Microsoft::UI::Xaml::Controls::ComboBox choiceComboBox(const RowSnapshot &row,
                                                              PaneHost &host);

// Writes a row's value through the model, commits, and queues a pane rebuild
// so gated rows re-derive — the immediate-apply save model the mac front end
// uses.
void setValueAndCommit(PaneHost &host, const QString &rowId, const QVariant &value);

} // namespace win
} // namespace speecher
