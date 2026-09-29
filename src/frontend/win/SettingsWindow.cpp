#include "frontend/win/SettingsWindow.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/UpdateBanner.h"
#include "app/UpdateController.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "frontend/win/HomePage.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/SettingsPage.h"
#include "frontend/win/ShortcutRecorder.h"
#include "providers/LocalModelStore.h"
#include "providers/TranscriptRefinementPrompt.h"
#include "frontend/win/TranscribePane.h"

#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <memory>

#include <windows.h>
#include <shellapi.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Input::FocusManager;
using winrt::Microsoft::UI::Xaml::Media::MicaBackdrop;

const QString kGeometrySetting = QStringLiteral("ui/settingsWindowGeometry");
const QString kWhatsNewPane = QStringLiteral("whatsNew");
const QString kHomePane = QStringLiteral("home");
// The Transcribe pane keeps its batch across the window; entering and leaving
// it tells TranscribePane so.
const QString kTranscribePane = QStringLiteral("transcribe");

// Segoe Fluent Icons for the schema's platform-neutral icon ids — the one
// piece of per-platform icon data this front end keeps.
wchar_t glyphForIconId(const QString &iconId)
{
    static const QHash<QString, wchar_t> glyphs = {
        {QStringLiteral("home"), L'\uE80F'},
        {QStringLiteral("settings"), L'\uE713'},
        {QStringLiteral("whatsNew"), L'\uE7E7'},
        {QStringLiteral("microphone"), L'\uE720'},
        {QStringLiteral("refinement"), L'\uE8D2'},
        {QStringLiteral("localModels"), L'\uE977'},
        {QStringLiteral("transcribe"), L'\uE8D6'},
        {QStringLiteral("output"), L'\uF0E3'},
        {QStringLiteral("vocabulary"), L'\uE82D'},
        {QStringLiteral("accounts"), L'\uE192'},
    };
    return glyphs.value(iconId, L'\uE713');
}

} // namespace

struct SettingsWindow::Native {
    Native(ApplicationController *controller, TranscribePane *transcribe)
        : controller(controller)
        , model(controller)
        , transcribe(transcribe)
    {
        host.model = &model;
        host.controller = controller;
        host.alive = alive;
        host.refresh = [this] { queueRebuild(); };
        host.action = [this](const QString &id) { runAction(id); };
        // Queued: the link that asks is inside the page the switch replaces.
        host.showPage = [this](const QString &id) {
            winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
                [this, id, weak = std::weak_ptr<bool>(alive)] {
                    if (!gone(weak) && window) {
                        showPage(id);
                    }
                });
        };
        host.hwnd = [this] { return windowHandle(); };
        host.xamlRoot = [this] {
            return root ? root.XamlRoot() : winrt::Microsoft::UI::Xaml::XamlRoot{nullptr};
        };
        host.effectiveTheme = [this] {
            return root ? root.ActualTheme() : ElementTheme::Default;
        };
        model.themeChanged = [this] {
            applyTheme();
            if (themeHook) {
                themeHook();
            }
        };
        model.capabilitiesChanged = [this] { queueRebuild(); };
        model.anthropicCredentialsChanged = [this] { queueRebuild(); };
        // The Check for updates row follows the banner's state too.
        QObject::connect(controller->updateBanner(),
                         &UpdateBanner::changed,
                         &lifetime,
                         [this] {
                             refreshBanner();
                             if (currentPane == QStringLiteral("general")) {
                                 queueRebuild();
                             }
                         });
        QObject::connect(controller,
                         &ApplicationController::whatsNewChanged,
                         &lifetime,
                         [this] {
                             refreshBanner();
                             rebuildSidebar();
                         });
        // What LocalSetup learns shows up in rows on three pages: endpoint
        // verdicts, runners, the hardware and the model list. It also writes
        // settings itself (the model in use after a delete, Speed Test
        // results, a runner), which the draft takes in first.
        QObject::connect(controller->localSetup(), &LocalSetup::changed, &lifetime, [this] {
            // A closed window re-reads the store when it opens.
            if (!window) {
                return;
            }
            model.syncWithStore();
            static const QStringList livePages{QStringLiteral("dictation"), QStringLiteral("refinement"),
                                               QStringLiteral("localModels")};
            if (livePages.contains(currentPane)) {
                queueLiveRebuild();
            }
        });
        // Only Home shows these; rebuilding another pane mid-dictation would
        // take focus from a field being dictated into.
        const auto rebuildHome = [this] {
            if (currentPane == kHomePane) {
                queueRebuild();
            }
        };
        // Every state change, and a delivery's receipt, which changes only
        // the status line.
        QObject::connect(controller, &ApplicationController::statusChanged, &lifetime, rebuildHome);
        QObject::connect(controller->insightsLog(), &InsightsLog::changed, &lifetime, rebuildHome);
        QObject::connect(controller, &ApplicationController::lastRecordChanged, &lifetime, rebuildHome);
    }

    ~Native()
    {
        // Dispatcher callbacks queued by this object can still be pending;
        // they hold a weak copy of this token and bail once it is cleared.
        *alive = false;
        transcribe->forget(host);
        // The Closed token is revoked before Close(), so windowClosed() never
        // runs on this path; quitting with the window open must still save its
        // geometry and give a suspended hotkey back.
        if (window) {
            saveGeometry();
            ShortcutRecorder::setRecording(host, false);
            window.Closed(closedToken);
            window.Close();
        }
    }

    HWND windowHandle() const
    {
        if (!window) {
            return nullptr;
        }
        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        return handle;
    }

    void show()
    {
        if (window) {
            // Selecting Home rebuilds it, which re-reads today; bringing an
            // open window back must too, or yesterday's streak stays up.
            if (currentPane == kHomePane) {
                queueRebuild();
            }
            window.Activate();
            SetForegroundWindow(windowHandle());
            return;
        }
        // Edits left from the last showing are not edits any more.
        model.reloadDraft();
        // A window opened from closed starts on Home.
        currentPane = kHomePane;
        createWindow();
        SetForegroundWindow(windowHandle());
    }

    void createWindow()
    {
        window = Window();
        window.SystemBackdrop(MicaBackdrop());
        window.ExtendsContentIntoTitleBar(true);
        window.Title(L"Speecher");

        root = Grid();
        RowDefinition titleRow;
        titleRow.Height({0, GridUnitType::Auto});
        RowDefinition contentRow;
        contentRow.Height({1, GridUnitType::Star});
        root.RowDefinitions().Append(titleRow);
        root.RowDefinitions().Append(contentRow);
        // The code-resolved secondary brushes follow the theme only through a
        // rebuild; this also covers the system flipping while set to System.
        root.ActualThemeChanged([this](const auto &, const auto &) { queueRebuild(); });
        // A live rebuild held back for a text field goes ahead once focus
        // has settled somewhere else.
        root.LostFocus([this](const auto &, const auto &) {
            if (!liveRebuildPending) {
                return;
            }
            liveRebuildPending = false;
            winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
                [this, weak = std::weak_ptr<bool>(alive)] {
                    if (!gone(weak)) {
                        queueLiveRebuild();
                    }
                });
        });

        titleBar = TitleBar();
        titleBar.Title(L"Speecher");
        setWindowIcon(window, titleBar);
        titleBar.IsBackButtonVisible(false);
        titleBar.BackRequested([this](const auto &, const auto &) { leaveWhatsNew(); });
        root.Children().Append(titleBar);

        navigation = NavigationView();
        navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navigation.IsPaneToggleButtonVisible(false);
        navigation.IsSettingsVisible(false);
        navigation.AlwaysShowHeader(false);
        search = AutoSuggestBox();
        search.PlaceholderText(L"Find a setting");
        SymbolIcon find;
        find.Symbol(Symbol::Find);
        search.QueryIcon(find);
        search.TextChanged([this](const AutoSuggestBox &sender,
                                  const AutoSuggestBoxTextChangedEventArgs &args) {
            if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) {
                return;
            }
            query = qs(sender.Text());
            rebuildSidebar();
        });
        // Enter opens the first hit.
        search.QuerySubmitted([this](const AutoSuggestBox &, const auto &) {
            const QStringList hits = model.searchPanes(query);
            if (!hits.isEmpty()) {
                showPage(hits.first());
            }
        });
        navigation.AutoSuggestBox(search);
        navigation.SelectionChanged([this](const NavigationView &, const auto &args) {
            if (sidebarUpdating) {
                return;
            }
            const auto item = args.SelectedItem();
            if (!item) {
                return;
            }
            // Through showPage, so picking What's New here is the same as any
            // other way of opening it.
            const QString id = qs(unbox_value<hstring>(item.as<NavigationViewItem>().Tag()));
            if (id != currentPane) {
                showPage(id);
            }
        });
        Grid::SetRow(navigation, 1);
        root.Children().Append(navigation);

        Grid content;
        RowDefinition bannerRow;
        bannerRow.Height({0, GridUnitType::Auto});
        RowDefinition pageRow;
        pageRow.Height({1, GridUnitType::Star});
        content.RowDefinitions().Append(bannerRow);
        content.RowDefinitions().Append(pageRow);
        banner = InfoBar();
        banner.IsOpen(false);
        banner.Margin({36, 12, 36, 0});
        banner.CloseButtonClick([this](const auto &, const auto &) {
            if (bannerCloseAction) {
                bannerCloseAction();
            }
        });
        content.Children().Append(banner);
        pageHost = Border();
        Grid::SetRow(pageHost, 1);
        content.Children().Append(pageHost);
        navigation.Content(content);

        window.Content(root);
        window.SetTitleBar(titleBar);
        applyTheme();
        restoreGeometry();
        closedToken = window.Closed([this](const auto &, const auto &) { windowClosed(); });

        rebuildSidebar();
        rebuildPage();
        refreshBanner();
        window.Activate();

        // The device enumeration and the keyring read would both delay the
        // first frame, so they wait a turn of the dispatcher for it.
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        queue.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                         [this, weak = std::weak_ptr<bool>(alive)] {
                             if (gone(weak) || !window) {
                                 return;
                             }
                             model.loadExpensiveRows();
                             controller->localSetup()->probeHardware();
                             controller->localSetup()->detectRunners();
                             rebuildPage();
                             // Only the keyring can stop to ask for an unlock,
                             // so it waits another turn.
                             winrt::Microsoft::UI::Dispatching::DispatcherQueue::
                                 GetForCurrentThread()
                                     .TryEnqueue([this, weak] {
                                         if (!gone(weak)) {
                                             loadApiKey();
                                         }
                                     });
                         });
    }

    void windowClosed()
    {
        saveGeometry();
        // The editors hold XAML trees of the window that is going away.
        host.editors.clear();
        host.localModels.reset();
        ShortcutRecorder::setRecording(host, false);
        transcribe->forget(host);
        window = nullptr;
        root = nullptr;
        titleBar = nullptr;
        navigation = nullptr;
        search = nullptr;
        banner = nullptr;
        pageHost = nullptr;
    }

    void restoreGeometry()
    {
        auto appWindow = window.AppWindow();
        const UINT dpi = GetDpiForWindow(windowHandle());
        const auto position = appWindow.Position();
        POINT origin{position.X, position.Y};
        int width = int(1100 * dpi / 96.0);
        int height = int(760 * dpi / 96.0);
        const QStringList parts = controller->settings()->raw().value(kGeometrySetting)
                                      .toString()
                                      .split(QLatin1Char(','));
        if (parts.size() == 4 && parts.at(2).toInt() > 0 && parts.at(3).toInt() > 0) {
            origin = {parts.at(0).toInt(), parts.at(1).toInt()};
            width = parts.at(2).toInt();
            height = parts.at(3).toInt();
        }

        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST), &monitor)) {
            return;
        }
        const RECT area = monitor.rcWork;
        const int availableWidth = area.right - area.left;
        const int availableHeight = area.bottom - area.top;
        // A partial intersection can still leave the titlebar off-screen.
        // Bound the entire window, including the scaled default on small screens.
        width = std::clamp(width,
                           std::min(GetSystemMetricsForDpi(SM_CXMINTRACK, dpi), availableWidth),
                           availableWidth);
        height = std::clamp(height,
                            std::min(GetSystemMetricsForDpi(SM_CYMINTRACK, dpi), availableHeight),
                            availableHeight);
        const int x = std::clamp(int(origin.x), int(area.left), int(area.right) - width);
        const int y = std::clamp(int(origin.y), int(area.top), int(area.bottom) - height);
        appWindow.MoveAndResize({x, y, width, height});
    }

    void saveGeometry()
    {
        if (!window) {
            return;
        }
        const auto appWindow = window.AppWindow();
        const auto position = appWindow.Position();
        const auto size = appWindow.Size();
        controller->settings()->raw().setValue(
            kGeometrySetting,
            QStringLiteral("%1,%2,%3,%4").arg(position.X).arg(position.Y)
                .arg(size.Width).arg(size.Height));
    }

    void applyTheme()
    {
        if (root) {
            root.RequestedTheme(requestedTheme(controller->settings()->theme()));
        }
    }

    NavigationViewItem sidebarItem(const QString &id, const QString &title, wchar_t glyph)
    {
        NavigationViewItem item;
        item.Content(box_value(hs(title)));
        item.Tag(box_value(hs(id)));
        FontIcon icon;
        icon.Glyph(hstring(std::wstring_view(&glyph, 1)));
        item.Icon(icon);
        return item;
    }

    void rebuildSidebar()
    {
        if (!navigation) {
            return;
        }
        sidebarUpdating = true;
        navigation.MenuItems().Clear();
        IInspectable selected{nullptr};
        const SettingsSchema &schema = model.schema();
        const auto append = [this, &selected, &schema](const QString &id) {
            const SettingsPane *pane = schema.pane(id);
            NavigationViewItem item = sidebarItem(id, pane->title, glyphForIconId(pane->iconId));
            if (id == currentPane) {
                selected = item;
            }
            navigation.MenuItems().Append(item);
        };
        if (!query.isEmpty()) {
            // A search lists its hits alone, from the core index.
            for (const QString &id : model.searchPanes(query)) {
                append(id);
            }
        } else {
            // What's New leads the untitled top group only while pending or
            // selected; each titled group sits under a NavigationViewItemHeader.
            if (SettingsWindow::offersWhatsNew(currentPane, controller->pendingWhatsNewVersion())) {
                append(kWhatsNewPane);
            }
            for (const SidebarGroup &group : schema.sidebarGroups) {
                if (!group.title.isEmpty()) {
                    NavigationViewItemHeader header;
                    header.Content(box_value(hs(group.title)));
                    navigation.MenuItems().Append(header);
                }
                for (const QString &id : group.panes) {
                    append(id);
                }
            }
        }
        navigation.SelectedItem(selected);
        sidebarUpdating = false;
    }

    void selectPane(const QString &id)
    {
        // The model browser belongs to its pane; left running, its download
        // progress would keep updating controls no longer on screen.
        if (id != currentPane) {
            host.localModels.reset();
        }
        if (id == kTranscribePane) {
            transcribe->enter();
        } else {
            transcribe->forget(host);
        }
        currentPane = id;
        if (titleBar) {
            titleBar.IsBackButtonVisible(id == kWhatsNewPane);
        }
        rebuildSidebar();
        rebuildPage();
    }

    // A page id, as resolvePage takes it; an unknown one shows Home.
    void showPage(const QString &pageId)
    {
        const PageId page = resolvePage(model.schema(), pageId);
        if (page.pane == kWhatsNewPane) {
            showWhatsNew();
            return;
        }
        if (!page.view.isEmpty()) {
            host.views.insert(page.pane, page.view);
        }
        selectPane(page.pane);
    }

    void showWhatsNew()
    {
        if (currentPane != kWhatsNewPane) {
            whatsNewReturnPane = currentPane;
        }
        controller->clearPendingWhatsNew();
        selectPane(kWhatsNewPane);
        refreshBanner();
    }

    void leaveWhatsNew()
    {
        selectPane(model.schema().pane(whatsNewReturnPane) ? whatsNewReturnPane : kHomePane);
    }

    void runAction(const QString &id)
    {
        if (id == QStringLiteral("whatsNew")) {
            showWhatsNew();
        } else if (id == QStringLiteral("speechLocalModelDownload")) {
            host.showPage(QStringLiteral("localModels"));
        } else if (id == QStringLiteral("resetCustomSystemPrompt")) {
            setValueAndCommit(host, QStringLiteral("customSystemPrompt"),
                              builtInDictationSystemPrompt());
        }
        // Every edit is already committed, so the draft is what is stored.
        controller->localSetup()->runSettingsAction(id, model.draft());
        if (actionHook) {
            actionHook(id);
        }
    }

    void queueRebuild()
    {
        if (rebuildQueued || !window) {
            return;
        }
        rebuildQueued = true;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
            [this, weak = std::weak_ptr<bool>(alive)] {
                if (gone(weak)) {
                    return;
                }
                rebuildQueued = false;
                if (window) {
                    rebuildPage();
                    refreshBanner();
                }
            });
    }

    // A rebuild for news from LocalSetup, which may land at any moment: never
    // while a text field has focus, whose half-typed value it would discard.
    void queueLiveRebuild()
    {
        if (editingText()) {
            liveRebuildPending = true;
            return;
        }
        queueRebuild();
    }

    bool editingText() const
    {
        if (!root || !root.XamlRoot()) {
            return false;
        }
        const auto focused = FocusManager::GetFocusedElement(root.XamlRoot());
        return focused && (focused.try_as<TextBox>() || focused.try_as<PasswordBox>());
    }

    void rebuildPage()
    {
        if (!pageHost) {
            return;
        }
        const SettingsPane *pane = model.schema().pane(currentPane);
        if (!pane) {
            return;
        }
        UIElement page{nullptr};
        try {
            switch (pane->layout) {
            case PaneLayout::Home:
                page = buildHomePage(host);
                break;
            case PaneLayout::Transcribe:
                page = transcribe->build(host, pane->title);
                break;
            case PaneLayout::Sections:
            case PaneLayout::Alternatives:
                page = buildPane(*pane, host);
                break;
            }
        } catch (const winrt::hresult_error &error) {
            // A throw from a dispatcher callback dies as a stowed exception
            // with no message anywhere; log it and keep the window alive.
            qWarning() << "settings pane" << currentPane << "failed to build:"
                       << QString::number(static_cast<quint32>(error.code()), 16)
                       << QString::fromWCharArray(error.message().c_str());
            return;
        }
        replacePage(pageHost, page);
    }

    void loadApiKey()
    {
        if (!window) {
            return;
        }
        const int edits = host.apiKeyEdits;
        const QString key = model.readApiKey();
        host.apiKeyLoaded = true;
        if (edits == host.apiKeyEdits) {
            host.apiKey = key;
            if (currentPane == QStringLiteral("accounts")) {
                rebuildPage();
            }
        }
    }

    // The update banner as core words it, drawn as an InfoBar: the primary
    // action and Later as its action buttons, download progress as its
    // content, and Dismiss as its close button. The What's New offer takes
    // its place when nothing else is showing.
    void refreshBanner()
    {
        if (!banner) {
            return;
        }
        banner.Content(nullptr);
        banner.ActionButton(nullptr);
        bannerCloseAction = {};
        const auto button = [](const QString &caption, bool enabled, std::function<void()> run) {
            Button control;
            control.Content(box_value(hs(caption)));
            control.IsEnabled(enabled);
            control.Click([run = std::move(run)](const auto &, const auto &) { run(); });
            return control;
        };

        const UpdateBannerModel update = controller->updateBanner()->model();
        if (!update.visible && !controller->pendingWhatsNewVersion().isEmpty()) {
            const WhatsNewBannerModel whatsNew =
                whatsNewBanner(controller->updates()->currentVersion());
            banner.Severity(InfoBarSeverity::Success);
            banner.Message(hs(whatsNew.text));
            banner.ActionButton(button(whatsNew.action, true, [this] { showWhatsNew(); }));
            banner.IsClosable(true);
            bannerCloseAction = [this] { controller->clearPendingWhatsNew(); };
            banner.IsOpen(true);
            return;
        }
        if (!update.visible) {
            banner.IsOpen(false);
            return;
        }
        banner.Severity(update.tone == UpdateBannerModel::Tone::Error ? InfoBarSeverity::Error
                        : update.tone == UpdateBannerModel::Tone::Positive
                            ? InfoBarSeverity::Success
                            : InfoBarSeverity::Informational);
        banner.Message(hs(update.text));
        UpdateBanner *model = controller->updateBanner();
        if (!update.action.isEmpty()) {
            banner.ActionButton(
                button(update.action, update.actionEnabled, [model] { model->runAction(); }));
        }
        // The InfoBar has one action button, so Later and the download's
        // progress go in its content row beneath the message.
        if (update.progress >= 0) {
            ProgressBar progress;
            progress.Minimum(0);
            progress.Maximum(100);
            progress.Value(update.progress);
            banner.Content(progress);
        } else if (!update.later.isEmpty()) {
            banner.Content(button(update.later, true, [model] { model->later(); }));
        }
        banner.IsClosable(!update.dismiss.isEmpty());
        bannerCloseAction = [model] { model->dismiss(); };
        banner.IsOpen(true);
    }

    bool capture(const QString &path)
    {
        if (!window) {
            return false;
        }
        // SPEECHER_GRAB_SIZE=WxH sizes the client area in DIPs, as on Qt and macOS.
        const QStringList size = qEnvironmentVariable("SPEECHER_GRAB_SIZE").split(QLatin1Char('x'));
        if (size.size() == 2) {
            const double scale = GetDpiForWindow(windowHandle()) / 96.0;
            window.AppWindow().ResizeClient(
                {int(size.at(0).toInt() * scale), int(size.at(1).toInt() * scale)});
        }
        const QString request = qEnvironmentVariable("SPEECHER_GRAB_PAGE");
        if (!request.isEmpty()) {
            showPage(request);
        }
        // Let composition catch up with the pane switch before printing.
        QEventLoop settle;
        QTimer::singleShot(250, &settle, &QEventLoop::quit);
        settle.exec();
        // SPEECHER_GRAB_SCROLL=bottom shows the end of the page, as on the
        // other platforms; "middle" shows what lies between, which a window
        // this short would otherwise never capture.
        const QString scrollTo = qEnvironmentVariable("SPEECHER_GRAB_SCROLL");
        if (scrollTo == QStringLiteral("bottom") || scrollTo == QStringLiteral("middle")) {
            if (const auto scroll = pageHost.Child().try_as<ScrollViewer>()) {
                const double end = scroll.ScrollableHeight();
                scroll.ChangeView(nullptr, scrollTo == QStringLiteral("middle") ? end * 0.6 : end,
                                  nullptr, true);
                QTimer::singleShot(250, &settle, &QEventLoop::quit);
                settle.exec();
            }
        }
        return printWindowTo(windowHandle(), path);
    }

    void confirm(const QString &title,
                 const QString &text,
                 const QString &confirmLabel,
                 std::function<void()> confirmed)
    {
        if (!root) {
            return;
        }
        ContentDialog dialog;
        dialog.XamlRoot(root.XamlRoot());
        // The dialog opens in the popup layer, outside the root's RequestedTheme.
        dialog.RequestedTheme(root.ActualTheme());
        dialog.Title(box_value(hs(title)));
        dialog.Content(box_value(hs(text)));
        dialog.PrimaryButtonText(hs(confirmLabel));
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(ContentDialogButton::Close);
        // On Closed rather than PrimaryButtonClick, so `confirmed` may open
        // a dialog of its own: WinUI allows one ContentDialog at a time.
        dialog.Closed([confirmed = std::move(confirmed),
                       weak = std::weak_ptr<bool>(alive)](const ContentDialog &,
                                                          const ContentDialogClosedEventArgs &args) {
            if (args.Result() == ContentDialogResult::Primary && !gone(weak)) {
                confirmed();
            }
        });
        dialog.ShowAsync();
    }

    void inform(const QString &title)
    {
        if (!root) {
            return;
        }
        ContentDialog dialog;
        dialog.XamlRoot(root.XamlRoot());
        dialog.RequestedTheme(root.ActualTheme());
        dialog.Title(box_value(hs(title)));
        dialog.CloseButtonText(L"OK");
        dialog.ShowAsync();
    }

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    ApplicationController *controller;
    SettingsModel model;
    PaneHost host;
    // Shared with the Transcribe window; holds its own state across rebuilds
    // and reopenings, like host does.
    TranscribePane *transcribe;
    std::function<void(const QString &)> actionHook;
    std::function<void()> themeHook;
    QObject lifetime;

    Window window{nullptr};
    winrt::event_token closedToken{};
    Grid root{nullptr};
    TitleBar titleBar{nullptr};
    NavigationView navigation{nullptr};
    AutoSuggestBox search{nullptr};
    InfoBar banner{nullptr};
    Border pageHost{nullptr};

    QString currentPane;
    QString whatsNewReturnPane;
    QString query;
    bool sidebarUpdating = false;
    bool rebuildQueued = false;
    bool liveRebuildPending = false;

    std::function<void()> bannerCloseAction;
};

SettingsWindow::SettingsWindow(ApplicationController *controller, TranscribePane *transcribe)
    : m_native(std::make_unique<Native>(controller, transcribe))
{
}

SettingsWindow::~SettingsWindow() = default;

bool SettingsWindow::offersWhatsNew(const QString &currentPane, const QString &pendingVersion)
{
    return currentPane == kWhatsNewPane || !pendingVersion.isEmpty();
}

void SettingsWindow::show()
{
    m_native->show();
}

void SettingsWindow::showPage(const QString &pageId)
{
    m_native->show();
    m_native->showPage(pageId);
}

void SettingsWindow::showWhatsNew()
{
    m_native->show();
    m_native->showWhatsNew();
}

void SettingsWindow::close()
{
    if (m_native->window) {
        m_native->window.Close();
    }
}

bool SettingsWindow::isVisible() const
{
    const HWND handle = m_native->windowHandle();
    return handle && IsWindowVisible(handle);
}

bool SettingsWindow::capture(const QString &path)
{
    return m_native->capture(path);
}

void SettingsWindow::confirm(const QString &title,
                             const QString &text,
                             const QString &confirmLabel,
                             std::function<void()> confirmed)
{
    m_native->confirm(title, text, confirmLabel, std::move(confirmed));
}

void SettingsWindow::inform(const QString &title)
{
    m_native->inform(title);
}

void SettingsWindow::setActionHook(std::function<void(const QString &)> hook)
{
    m_native->actionHook = std::move(hook);
}

void SettingsWindow::setThemeHook(std::function<void()> hook)
{
    m_native->themeHook = std::move(hook);
}

} // namespace speecher::win
