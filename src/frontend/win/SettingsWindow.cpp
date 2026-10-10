#include "frontend/win/SettingsWindow.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PhoneTransfer.h"
#include "app/PhoneTransferPresentation.h"
#include "frontend/win/CustomRows.h"
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

#include <QClipboard>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <windows.h>
#include <shellapi.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
// The headers above declared the automation peers before windows.h renamed
// GetClassName, so their definitions must not see the rename either.
#pragma push_macro("GetClassName")
#undef GetClassName
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma pop_macro("GetClassName")
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Automation::AutomationProperties;
using winrt::Microsoft::UI::Xaml::Input::FocusManager;
using winrt::Microsoft::UI::Xaml::Media::MicaBackdrop;
using winrt::Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap;

const QString kGeometrySetting = QStringLiteral("ui/settingsWindowGeometry");
// The open sidebar, narrower than NavigationView's 320 to leave the page room.
constexpr int kPaneLength = 240;
// Under half of a 1080p screen at 125% (768), so Snap can put the window
// beside another. The page then keeps, beside the pane and inside its gutters, a
// card wide enough for the widest control below its title.
constexpr int kMinimumWidth = 760;
constexpr int kMinimumHeight = 480;
const QString kWhatsNewPane = QStringLiteral("whatsNew");
const QString kHomePane = QStringLiteral("home");
// The Transcribe pane keeps its batch across the window; entering and leaving
// it tells TranscribePane so.
const QString kTranscribePane = QStringLiteral("transcribe");

// The phone transfer's code, in DIPs: large enough to scan from across a desk.
constexpr double kPhoneCodeSide = 240;
// Wider than ContentDialog's 548 default, so the lists keep room beside the code.
constexpr double kPhoneTransferDialogWidth = 680;
// A spent code at the opacity of the theme's disabled text (#5C).
constexpr double kSpentCodeOpacity = 0.36;

// The code as an image one bitmap pixel per screen pixel, so no module blurs.
Image phoneCode(const QString &link, double scale)
{
    const QImage image = qrCodeImage(link, int(kPhoneCodeSide * scale))
                             .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    WriteableBitmap bitmap(image.width(), image.height());
    // BGRA8, premultiplied: QImage's ARGB32 on a little-endian machine.
    std::memcpy(bitmap.PixelBuffer().data(), image.constBits(), size_t(image.sizeInBytes()));
    bitmap.Invalidate();
    Image code;
    code.Source(bitmap);
    code.Width(image.width() / scale);
    code.Height(image.height() / scale);
    code.VerticalAlignment(VerticalAlignment::Top);
    return code;
}

// items one per line after a bullet or their number, wrapped lines indented
// past it, as an HTML list sets them.
StackPanel markedList(const QStringList &items, bool numbered)
{
    StackPanel list;
    list.Spacing(4);
    for (qsizetype index = 0; index < items.size(); ++index) {
        Grid item;
        item.ColumnSpacing(8);
        ColumnDefinition markerColumn;
        markerColumn.Width({0, GridUnitType::Auto});
        item.ColumnDefinitions().Append(markerColumn);
        item.ColumnDefinitions().Append(ColumnDefinition{});
        item.Children().Append(styledTextBlock(
            numbered ? QStringLiteral("%1.").arg(index + 1) : QStringLiteral("\u2022"), L"BodyTextBlockStyle"));
        TextBlock text = styledTextBlock(items.at(index), L"BodyTextBlockStyle");
        Grid::SetColumn(text, 1);
        item.Children().Append(text);
        list.Children().Append(item);
    }
    return list;
}

StackPanel listSection(const QString &heading, const QStringList &items)
{
    StackPanel section;
    section.Spacing(4);
    section.Children().Append(styledTextBlock(heading, L"BodyStrongTextBlockStyle"));
    section.Children().Append(markedList(items, false));
    return section;
}

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
        {QStringLiteral("writingProfiles"), L'\uE70F'},
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
        // `speecher vocabulary add` saves terms through the running app.
        QObject::connect(controller->settings(), &SettingsStore::vocabularyAdded, &lifetime, [this] {
            if (!window) {
                return;
            }
            model.syncWithStore();
            if (currentPane == QStringLiteral("vocabulary")) {
                queueLiveRebuild();
            }
        });
        // A microphone plugged in or taken out changes the Input device row's
        // choices, and whether it has any.
        QObject::connect(new QMediaDevices(&lifetime), &QMediaDevices::audioInputsChanged, &lifetime, [this] {
            if (!window) {
                return;
            }
            model.refreshAudioInput();
            if (currentPane == QStringLiteral("dictation")) {
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
        endMicrophoneTest(host);
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
        currentSubpage.clear();
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
        titleBar.BackRequested([this](const auto &, const auto &) { goBack(); });
        root.Children().Append(titleBar);

        navigation = NavigationView();
        // Always open. The adaptive Auto mode is not used: every pane switch
        // while it was compact ended in a layout cycle.
        navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        navigation.OpenPaneLength(kPaneLength);
        navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navigation.IsPaneToggleButtonVisible(false);
        navigation.IsSettingsVisible(false);
        navigation.AlwaysShowHeader(false);
        search = AutoSuggestBox();
        search.PlaceholderText(L"Find a setting");
        // The suggestions are elements, whose text is no query to type over
        // the box with.
        search.UpdateTextOnSelect(false);
        SymbolIcon find;
        find.Symbol(Symbol::Find);
        search.QueryIcon(find);
        search.TextChanged([this](const AutoSuggestBox &sender,
                                  const AutoSuggestBoxTextChangedEventArgs &args) {
            if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) {
                return;
            }
            const QString query = qs(sender.Text()).trimmed();
            sender.ItemsSource(query.isEmpty() ? IInspectable{nullptr} : IInspectable(searchSuggestions(query)));
        });
        // A chosen suggestion opens its row; Enter opens the first one.
        search.QuerySubmitted([this](const AutoSuggestBox &,
                                     const AutoSuggestBoxQuerySubmittedEventArgs &args) {
            if (const auto chosen = args.ChosenSuggestion()) {
                const auto hit = chosen.try_as<FrameworkElement>();
                if (hit && hit.Tag()) {
                    const QString target = qs(unbox_value<hstring>(hit.Tag()));
                    showSearchHit(target.section(QLatin1Char('\n'), 0, 0),
                                  target.section(QLatin1Char('\n'), 1));
                }
                return;
            }
            const QList<SearchMatch> matches = model.search(qs(args.QueryText()));
            if (!matches.isEmpty()) {
                showSearchHit(matches.first().pane, matches.first().rows.value(0));
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
        // The pane stays selected while its subpage shows, so choosing it
        // again is the way back, as in the Settings app.
        navigation.ItemInvoked([this](const NavigationView &, const NavigationViewItemInvokedEventArgs &args) {
            const auto item = args.InvokedItemContainer();
            if (item && item.Tag() && !currentSubpage.isEmpty()
                && qs(unbox_value<hstring>(item.Tag())) == currentPane) {
                selectPane(currentPane);
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
        // The page column's width, so the banner's edges are the cards'.
        banner.Margin({kPageGutter, 12, kPageGutter, 0});
        banner.MaxWidth(kPageColumnWidth);
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
        if (const auto presenter =
                window.AppWindow().Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()) {
            const double scale = GetDpiForWindow(windowHandle()) / 96.0;
            presenter.PreferredMinimumWidth(int(kMinimumWidth * scale));
            presenter.PreferredMinimumHeight(int(kMinimumHeight * scale));
        }
        // Back from the Windows privacy page the microphone note sends people
        // to, the Input device row asks again. Other activations do not
        // enumerate devices.
        window.Activated([this](const auto &, const WindowActivatedEventArgs &args) {
            if (args.WindowActivationState() != WindowActivationState::Deactivated
                && std::exchange(microphoneSettingsOpened, false) && model.refreshAudioInput()) {
                queueRebuild();
            }
        });
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
        endMicrophoneTest(host);
        ShortcutRecorder::setRecording(host, false);
        transcribe->forget(host);
        // The phone transfer's dialog went with the window, so its port closes too.
        phoneTransfer.reset();
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
        width = std::clamp(width, std::min(int(kMinimumWidth * dpi / 96.0), availableWidth), availableWidth);
        height = std::clamp(height, std::min(int(kMinimumHeight * dpi / 96.0), availableHeight),
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
        navigation.SelectedItem(selected);
        sidebarUpdating = false;
    }

    // A pane, or one of its subpages, which keeps the pane selected in the
    // sidebar.
    void selectPane(const QString &id, const QString &subpage = {})
    {
        // The model browser belongs to its pane; left running, its download
        // progress would keep updating controls no longer on screen. Another
        // page opens at its top, not at the scroll offset of this one.
        if (id != currentPane || subpage != currentSubpage) {
            host.localModels.reset();
            endMicrophoneTest(host);
            scrollToTop = true;
        }
        currentSubpage = subpage;
        if (id == kTranscribePane) {
            transcribe->enter();
        } else {
            transcribe->forget(host);
        }
        currentPane = id;
        if (titleBar) {
            titleBar.IsBackButtonVisible(id == kWhatsNewPane || !subpage.isEmpty());
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
        selectPane(page.pane, page.subpage);
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

    // The search's suggestions: each matching row under its pane's title, or
    // the pane alone when only its title or a heading matched. Each carries
    // "pane\nrow" as its tag; with no match, one untagged line says so.
    winrt::Windows::Foundation::Collections::IVector<IInspectable> searchSuggestions(const QString &query)
    {
        auto items = winrt::single_threaded_vector<IInspectable>();
        const SettingsSchema &schema = model.schema();
        const auto suggestion = [this, &items](const QString &title, const QString &paneTitle,
                                               const QString &target) {
            StackPanel item;
            item.Padding({0, 4, 0, 4});
            item.Tag(box_value(hs(target)));
            item.Children().Append(styledTextBlock(title, L"SettingsCardBodyStyle"));
            if (!paneTitle.isEmpty()) {
                item.Children().Append(secondaryTextBlock(paneTitle, L"SettingsCardDescriptionStyle", host));
            }
            QStringList name{title, paneTitle};
            name.removeAll(QString());
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
                item, hs(name.join(QStringLiteral(", "))));
            items.Append(item);
        };
        for (const SearchMatch &match : model.search(query)) {
            const SettingsPane *pane = schema.pane(match.pane);
            if (match.rows.isEmpty()) {
                suggestion(pane->title, QString(), match.pane);
            }
            for (const QString &rowId : match.rows) {
                // A row the section names, with no label of its own, goes by
                // the section's title.
                QString title = schema.row(rowId)->label;
                for (const SettingsPaneGroup &group : pane->groups) {
                    if (title.isEmpty() && group.rows.contains(rowId)) {
                        title = group.title;
                    }
                }
                suggestion(title, pane->title, match.pane + QLatin1Char('\n') + rowId);
            }
        }
        if (items.Size() == 0) {
            items.Append(secondaryTextBlock(noSettingsMatchText(), L"SettingsCardBodyStyle", host));
        }
        return items;
    }

    // Opens the pane a search found, on the view that holds the row, scrolled
    // to the row's card.
    void showSearchHit(const QString &paneId, const QString &rowId)
    {
        const SettingsPane *pane = model.schema().pane(paneId);
        if (!pane) {
            return;
        }
        for (const SettingsPaneGroup &group : pane->groups) {
            if (!group.view.isEmpty() && group.rows.contains(rowId)) {
                host.views.insert(paneId, group.view);
            }
        }
        host.revealRow = rowId;
        if (paneId == currentPane && currentSubpage.isEmpty()) {
            scrollToTop = true;
            rebuildPage();
        } else {
            selectPane(paneId);
        }
    }

    // Back leaves a subpage for its pane, and What's New for where it was
    // opened from.
    void goBack()
    {
        if (currentSubpage.isEmpty()) {
            leaveWhatsNew();
        } else {
            selectPane(currentPane);
        }
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
        } else if (id == QStringLiteral("openMicrophoneSettings")) {
            microphoneSettingsOpened = true;
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
        const SettingsSubpage *subpage = model.schema().subpage(currentSubpage);
        if (subpage && subpage->parent != currentPane) {
            subpage = nullptr;
        }
        UIElement page{nullptr};
        try {
            if (subpage) {
                page = buildSubpage(*subpage, host);
            } else {
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
            }
            // A sought row this pane did not show is not sought on the next.
            host.revealRow.clear();
        } catch (const winrt::hresult_error &error) {
            // A throw from a dispatcher callback dies as a stowed exception
            // with no message anywhere; log it and keep the window alive.
            qWarning() << "settings pane" << currentPane << "failed to build:"
                       << QString::number(static_cast<quint32>(error.code()), 16)
                       << QString::fromWCharArray(error.message().c_str());
            return;
        }
        replacePage(pageHost, page, !std::exchange(scrollToTop, false));
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
        // "stats-image" saves the picture Home's Share menu copies, read back
        // from the clipboard.
        if (request == QStringLiteral("stats-image")) {
            showPage(QStringLiteral("home"));
            // Shared, as a draw that outlasts the wait still reports here.
            const auto copied = std::make_shared<std::optional<bool>>();
            copyStatsImage(host, [copied](bool ok) { *copied = ok; });
            QEventLoop drawn;
            for (int waited = 0; !copied->has_value() && waited < 10000; waited += 50) {
                QTimer::singleShot(50, &drawn, &QEventLoop::quit);
                drawn.exec();
            }
            return copied->value_or(false) && QGuiApplication::clipboard()->image().save(path);
        }
        if (!request.isEmpty()) {
            showPage(request);
        }
        // Let composition catch up with the pane switch before printing: the
        // new page's first layout, which a busy first launch can hold back,
        // then a moment for its frame.
        QEventLoop settle;
        const auto page = pageHost.Child().try_as<FrameworkElement>();
        for (int waited = 0; page && !page.IsLoaded() && waited < 2000; waited += 50) {
            QTimer::singleShot(50, &settle, &QEventLoop::quit);
            settle.exec();
        }
        QTimer::singleShot(250, &settle, &QEventLoop::quit);
        settle.exec();
        // SPEECHER_GRAB_SCROLL=bottom shows the end of the page, and
        // =<pixels> the page that far down, as on the other platforms;
        // "middle" shows what lies between, which a window this short would
        // otherwise never capture.
        const QString scrollTo = qEnvironmentVariable("SPEECHER_GRAB_SCROLL");
        bool scrollIsPixels = false;
        const int scrollPixels = scrollTo.toInt(&scrollIsPixels);
        if (scrollTo == QStringLiteral("bottom") || scrollTo == QStringLiteral("middle") || scrollIsPixels) {
            if (const auto scroll = pageScroller(pageHost.Child())) {
                const double end = scroll.ScrollableHeight();
                const double offset = scrollIsPixels                         ? std::min<double>(scrollPixels, end)
                                      : scrollTo == QStringLiteral("middle") ? end * 0.6
                                                                             : end;
                scroll.ChangeView(nullptr, offset, nullptr, true);
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

    void inform(const QString &title, const QString &text)
    {
        if (!root) {
            return;
        }
        ContentDialog dialog;
        dialog.XamlRoot(root.XamlRoot());
        dialog.RequestedTheme(root.ActualTheme());
        dialog.Title(box_value(hs(title)));
        dialog.Content(box_value(hs(text)));
        dialog.CloseButtonText(L"Close");
        dialog.ShowAsync();
    }

    void showPhoneTransfer(const AppSettings &settings)
    {
        if (!root) {
            return;
        }
        phoneTransfer = std::make_unique<PhoneTransfer>(settings);
        PhoneTransfer *transfer = phoneTransfer.get();
        const PhoneTransferText text = phoneTransferText(settings, transfer->state());
        ContentDialog dialog;
        dialog.XamlRoot(root.XamlRoot());
        dialog.RequestedTheme(root.ActualTheme());
        dialog.Resources().Insert(box_value(L"ContentDialogMaxWidth"), box_value(kPhoneTransferDialogWidth));
        dialog.Title(box_value(hs(text.title)));
        dialog.CloseButtonText(hs(text.close));

        StackPanel details;
        details.Spacing(12);
        if (!text.steps.isEmpty()) {
            details.Children().Append(markedList(text.steps, true));
        }
        details.Children().Append(listSection(text.includedHeading, text.included));
        if (!text.stays.isEmpty()) {
            details.Children().Append(listSection(text.staysHeading, text.stays));
        }
        details.Children().Append(secondaryTextBlock(text.neverIncluded, L"CaptionTextBlockStyle", host));
        TextBlock status = styledTextBlock(text.status, L"BodyTextBlockStyle");
        details.Children().Append(status);

        // No code without an address or a port; the status says which.
        Image code{nullptr};
        if (const QString link = transfer->link(); !link.isEmpty()) {
            code = phoneCode(link, root.XamlRoot().RasterizationScale());
            Grid layout;
            layout.ColumnSpacing(24);
            ColumnDefinition codeColumn;
            codeColumn.Width({0, GridUnitType::Auto});
            layout.ColumnDefinitions().Append(codeColumn);
            layout.ColumnDefinitions().Append(ColumnDefinition{});
            layout.Children().Append(code);
            Grid::SetColumn(details, 1);
            layout.Children().Append(details);
            dialog.Content(layout);
        } else {
            dialog.Content(details);
        }

        // Once sent, the code is spent and dims.
        QObject::connect(transfer, &PhoneTransfer::stateChanged, transfer, [transfer, settings, status, code] {
            status.Text(hs(phoneTransferText(settings, transfer->state()).status));
            if (code) {
                code.Opacity(transfer->state() == PhoneTransferState::Waiting ? 1 : kSpentCodeOpacity);
            }
        });
        // The transfer listens exactly as long as its code is on screen.
        dialog.Closed([this, weak = std::weak_ptr<bool>(alive)](const ContentDialog &,
                                                                const ContentDialogClosedEventArgs &) {
            if (!gone(weak)) {
                phoneTransfer.reset();
            }
        });
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
    // The open "Copy settings to your phone" dialog's transfer.
    std::unique_ptr<PhoneTransfer> phoneTransfer;

    Window window{nullptr};
    winrt::event_token closedToken{};
    Grid root{nullptr};
    TitleBar titleBar{nullptr};
    NavigationView navigation{nullptr};
    AutoSuggestBox search{nullptr};
    InfoBar banner{nullptr};
    Border pageHost{nullptr};

    QString currentPane;
    // The subpage of currentPane on screen, by id; empty for the pane itself.
    QString currentSubpage;
    QString whatsNewReturnPane;
    bool sidebarUpdating = false;
    // The next build is another pane, or a search's row, not a rebuild of
    // the page on screen.
    bool scrollToTop = false;
    bool rebuildQueued = false;
    bool liveRebuildPending = false;
    // The Input device row sent the person to the privacy page; the next
    // activation asks for devices again.
    bool microphoneSettingsOpened = false;

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

void SettingsWindow::recheckMicrophonesOnReturn()
{
    // A window opened later enumerates the devices anyway.
    if (m_native->window) {
        m_native->microphoneSettingsOpened = true;
    }
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

void SettingsWindow::inform(const QString &title, const QString &text)
{
    m_native->inform(title, text);
}

void SettingsWindow::showPhoneTransfer(const AppSettings &settings)
{
    m_native->showPhoneTransfer(settings);
}

namespace {

// The controls of a page built in code, in order, from its panels, borders
// and content: the tree as built, which needs no layout pass.
void collectControls(const IInspectable &node, std::vector<Control> &found)
{
    if (!node) {
        return;
    }
    if (const auto control = node.try_as<Control>()) {
        found.push_back(control);
    }
    if (const auto panel = node.try_as<Panel>()) {
        for (const UIElement &child : panel.Children()) {
            collectControls(child, found);
        }
    } else if (const auto border = node.try_as<Border>()) {
        collectControls(border.Child(), found);
    } else if (const auto content = node.try_as<ContentControl>()) {
        collectControls(content.Content(), found);
    }
}

} // namespace

QString SettingsWindow::shownPageForTest() const
{
    return m_native->currentSubpage.isEmpty() ? m_native->currentPane : m_native->currentSubpage;
}

QString SettingsWindow::selectedPaneForTest() const
{
    const auto item = m_native->navigation ? m_native->navigation.SelectedItem() : nullptr;
    return item ? qs(unbox_value<hstring>(item.as<NavigationViewItem>().Tag())) : QString();
}

bool SettingsWindow::backVisibleForTest() const
{
    return m_native->titleBar && m_native->titleBar.IsBackButtonVisible();
}

void SettingsWindow::goBackForTest()
{
    m_native->goBack();
}

bool SettingsWindow::pressForTest(const QString &name, int index)
{
    std::vector<Control> controls;
    collectControls(m_native->pageHost ? m_native->pageHost.Child() : nullptr, controls);
    for (const Control &control : controls) {
        const auto button = control.try_as<Button>();
        if (button && qs(AutomationProperties::GetName(button)) == name && index-- == 0) {
            if (!button.IsEnabled()) {
                return false;
            }
            winrt::Microsoft::UI::Xaml::Automation::Peers::ButtonAutomationPeer(button).Invoke();
            return true;
        }
    }
    return false;
}

bool SettingsWindow::phoneTransferOpenForTest() const
{
    return m_native->phoneTransfer != nullptr;
}

bool SettingsWindow::closeDialogForTest()
{
    if (!m_native->root) {
        return false;
    }
    for (const auto &popup :
         winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(m_native->root.XamlRoot())) {
        if (const auto dialog = popup.Child().try_as<ContentDialog>()) {
            dialog.Hide();
            return true;
        }
    }
    return false;
}

bool SettingsWindow::chooseForTest(const QString &name, const QString &choice)
{
    std::vector<Control> controls;
    collectControls(m_native->pageHost ? m_native->pageHost.Child() : nullptr, controls);
    for (const Control &control : controls) {
        const auto combo = control.try_as<ComboBox>();
        if (!combo || qs(AutomationProperties::GetName(combo)) != name) {
            continue;
        }
        for (const IInspectable &item : combo.Items()) {
            if (qs(unbox_value<hstring>(item.as<ComboBoxItem>().Content())) == choice) {
                combo.SelectedItem(item);
                return true;
            }
        }
    }
    return false;
}

QStringList SettingsWindow::ratingBarsForTest() const
{
    std::vector<Control> controls;
    collectControls(m_native->pageHost ? m_native->pageHost.Child() : nullptr, controls);
    // Told from the other bars on a page, such as the microphone's level, by name.
    const QStringList measures{ratingMeasureLabel(RatingMeasure::Accuracy), ratingMeasureLabel(RatingMeasure::Quality),
                               ratingMeasureLabel(RatingMeasure::Speed)};
    QStringList bars;
    for (const Control &control : controls) {
        const auto bar = control.try_as<ProgressBar>();
        const QString name = bar ? qs(AutomationProperties::GetName(bar)) : QString();
        if (measures.contains(name)) {
            bars.append(QStringLiteral("%1 %2").arg(name).arg(bar.Value()));
        }
    }
    return bars;
}

namespace {

Expander namedExpander(const UIElement &page, const QString &name)
{
    std::vector<Control> controls;
    collectControls(page, controls);
    for (const Control &control : controls) {
        const auto expander = control.try_as<Expander>();
        if (expander && qs(AutomationProperties::GetName(expander)) == name) {
            return expander;
        }
    }
    return nullptr;
}

} // namespace

bool SettingsWindow::expandForTest(const QString &name)
{
    const Expander expander = namedExpander(m_native->pageHost ? m_native->pageHost.Child() : nullptr, name);
    if (expander) {
        expander.IsExpanded(true);
    }
    return bool(expander);
}

bool SettingsWindow::expandedForTest(const QString &name) const
{
    const Expander expander = namedExpander(m_native->pageHost ? m_native->pageHost.Child() : nullptr, name);
    return expander && expander.IsExpanded();
}

QStringList SettingsWindow::searchSuggestionsForTest(const QString &query)
{
    QStringList suggestions;
    for (const IInspectable &item : m_native->searchSuggestions(query)) {
        const auto element = item.as<FrameworkElement>();
        suggestions.append(element.Tag() ? qs(unbox_value<hstring>(element.Tag()))
                                         : qs(element.as<TextBlock>().Text()));
    }
    return suggestions;
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
