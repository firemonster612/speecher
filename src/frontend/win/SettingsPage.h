#pragma once

#include "frontend/win/Panes.h"

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
    // Collection editors by row id, kept across pane rebuilds so an undo
    // history survives an unrelated setting changing.
    QHash<QString, std::shared_ptr<CollectionEditor>> editors;
    // SelectorBar selection by pane id.
    QHash<QString, int> alternative;
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

// One pane as a WinUI page: ScrollViewer over a 1064-wide column with the page
// title, BodyStrong card headers, SettingsCard-shaped rows spaced 4 and
// Caption footnotes — the WinUI Gallery settings page, built in code.
winrt::Microsoft::UI::Xaml::UIElement buildPane(const Pane &pane,
                                                const QList<PageSnapshot> &pages,
                                                PaneHost &host);

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
