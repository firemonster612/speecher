#include "frontend/win/SettingsWindow.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/SettingsPage.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTimer>
#include <QUrl>

#include <windows.h>
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
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Microsoft::UI::Xaml::Media::MicaBackdrop;

const QString kPaneSetting = QStringLiteral("ui/settingsPane");
const QString kGeometrySetting = QStringLiteral("ui/settingsWindowGeometry");
const QString kWhatsNewPane = QStringLiteral("whatsNew");
// The global-shortcut recorder is the sole capability page with no schema page
// behind it; everything else in the sidebar comes from the schema.
const QString kShortcutPane = QStringLiteral("shortcut");
const QString kShortcutTitle = QStringLiteral("Shortcut");

// Segoe Fluent Icons for the schema's platform-neutral icon ids — the one
// piece of per-platform icon data this front end keeps.
wchar_t glyphForIconId(const QString &iconId)
{
    static const QHash<QString, wchar_t> glyphs = {
        {QStringLiteral("settings"), L'\uE713'},
        {QStringLiteral("whatsNew"), L'\uE7E7'},
        {QStringLiteral("microphone"), L'\uE720'},
        {QStringLiteral("text"), L'\uE8D2'},
        {QStringLiteral("clipboard"), L'\uF0E3'},
        {QStringLiteral("dictionary"), L'\uE82D'},
        {QStringLiteral("checkmark"), L'\uE73E'},
        {QStringLiteral("swap"), L'\uE8AB'},
        {QStringLiteral("key"), L'\uE192'},
        {QStringLiteral("shortcut"), L'\uE765'},
    };
    return glyphs.value(iconId, L'\uE713');
}

const PageSnapshot *pageWithId(const QList<PageSnapshot> &pages, const QString &id)
{
    for (const PageSnapshot &page : pages) {
        if (page.id == id) {
            return &page;
        }
    }
    return nullptr;
}

// Whether anything on a page — its title, a section heading, a row, or the
// help under one — answers to the query.
bool pageMatches(const PageSnapshot &page, const QString &query)
{
    const auto hit = [needle = query.toLower()](const QString &text) {
        return text.toLower().contains(needle);
    };
    if (hit(page.title)) {
        return true;
    }
    for (const SectionSnapshot &section : page.sections) {
        if (hit(section.title) || hit(section.help)) {
            return true;
        }
        for (const RowSnapshot &row : section.rows) {
            if (hit(row.label) || hit(row.help)) {
                return true;
            }
        }
    }
    return false;
}

// The window's client pixels through PrintWindow, which composes the swap
// chain content DWM holds — RenderTargetBitmap misses the backdrop.
bool printWindowTo(HWND handle, const QString &path)
{
    RECT rect{};
    if (!GetWindowRect(handle, &rect)) {
        return false;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    const HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    const auto previous = SelectObject(memory, bitmap);
    constexpr UINT renderFullContent = 0x00000002; // PW_RENDERFULLCONTENT
    const bool painted = PrintWindow(handle, memory, renderFullContent) != 0;
    bool saved = false;
    if (painted) {
        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (GetDIBits(memory, bitmap, 0, height, image.bits(), &info, DIB_RGB_COLORS)) {
            saved = image.save(path);
        }
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return saved;
}

} // namespace

struct SettingsWindow::Native {
    explicit Native(ApplicationController *controller)
        : controller(controller)
        , model(controller)
    {
        host.model = &model;
        host.controller = controller;
        host.refresh = [this] { queueRebuild(); };
        host.action = [this](const QString &id) { runAction(id); };
        host.hwnd = [this] { return windowHandle(); };
        host.xamlRoot = [this] {
            return root ? root.XamlRoot() : winrt::Microsoft::UI::Xaml::XamlRoot{nullptr};
        };
        host.effectiveTheme = [this] {
            return root ? root.ActualTheme() : ElementTheme::Default;
        };
        model.themeChanged = [this] { applyTheme(); };
        model.capabilitiesChanged = [this] { queueRebuild(); };
        model.anthropicCredentialsChanged = [this] { queueRebuild(); };
        QObject::connect(controller->updates(),
                         &UpdateController::changed,
                         &lifetime,
                         [this] { refreshBanner(); });
        QObject::connect(controller,
                         &ApplicationController::whatsNewChanged,
                         &lifetime,
                         [this] {
                             refreshBanner();
                             rebuildSidebar();
                         });
    }

    ~Native()
    {
        if (window) {
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
            window.Activate();
            SetForegroundWindow(windowHandle());
            return;
        }
        // Edits left from the last showing are not edits any more.
        model.reloadDraft();
        currentPane = controller->settings()->raw().value(kPaneSetting).toString();
        if (currentPane != kShortcutPane && !pageWithId(model.pages(), currentPane)) {
            currentPane = model.pages().first().id;
        }
        // What's New only exists while pending or selected; a remembered
        // selection of it stays honoured.
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

        titleBar = TitleBar();
        titleBar.Title(L"Speecher");
        const QString icon = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("speecher.ico"));
        if (QFile::exists(icon)) {
            ImageIconSource iconSource;
            iconSource.ImageSource(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage(
                winrt::Windows::Foundation::Uri(hs(QUrl::fromLocalFile(icon).toString()))));
            titleBar.IconSource(iconSource);
            window.AppWindow().SetIcon(hs(QDir::toNativeSeparators(icon)));
        }
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
        navigation.AutoSuggestBox(search);
        navigation.SelectionChanged([this](const NavigationView &, const auto &args) {
            if (sidebarUpdating) {
                return;
            }
            const auto item = args.SelectedItem();
            if (!item) {
                return;
            }
            const QString id = qs(unbox_value<hstring>(item.as<NavigationViewItem>().Tag()));
            if (id != currentPane) {
                selectPane(id);
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
                         [this] {
                             if (!window) {
                                 return;
                             }
                             model.loadExpensiveRows();
                             rebuildPage();
                             // Only the keyring can stop to ask for an unlock,
                             // so it waits another turn.
                             winrt::Microsoft::UI::Dispatching::DispatcherQueue::
                                 GetForCurrentThread()
                                     .TryEnqueue([this] { loadApiKey(); });
                         });
    }

    void windowClosed()
    {
        saveGeometry();
        // The editors hold XAML trees of the window that is going away.
        host.editors.clear();
        host.shortcutRecording = false;
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
        const QStringList parts = controller->settings()->raw()
                                      .value(kGeometrySetting)
                                      .toString()
                                      .split(QLatin1Char(','));
        auto appWindow = window.AppWindow();
        if (parts.size() == 4) {
            appWindow.MoveAndResize({parts.at(0).toInt(), parts.at(1).toInt(),
                                     parts.at(2).toInt(), parts.at(3).toInt()});
            return;
        }
        appWindow.Resize({1100, 760});
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
        const auto append = [this, &selected](const QString &id,
                                              const QString &title,
                                              wchar_t glyph) {
            NavigationViewItem item = sidebarItem(id, title, glyph);
            if (id == currentPane) {
                selected = item;
            }
            navigation.MenuItems().Append(item);
        };
        const QList<PageSnapshot> pages = model.pages();
        // What's New sits on top only while pending or selected, as before.
        if (query.isEmpty()
            && SettingsWindow::offersWhatsNew(currentPane,
                                              controller->pendingWhatsNewVersion())) {
            if (const PageSnapshot *whatsNew = pageWithId(pages, kWhatsNewPane)) {
                append(whatsNew->id, whatsNew->title, glyphForIconId(whatsNew->iconId));
                navigation.MenuItems().Append(NavigationViewItemSeparator());
            }
        }
        // One item per schema page, in schema order; a search filters them.
        for (const PageSnapshot &page : pages) {
            if (page.id == kWhatsNewPane) {
                continue;
            }
            if (!query.isEmpty() && !pageMatches(page, query)) {
                continue;
            }
            append(page.id, page.title, glyphForIconId(page.iconId));
        }
        // The shortcut recorder, the sole non-schema capability page.
        if (query.isEmpty() || kShortcutTitle.toLower().contains(query.toLower())) {
            append(kShortcutPane, kShortcutTitle, glyphForIconId(kShortcutPane));
        }
        navigation.SelectedItem(selected);
        sidebarUpdating = false;
    }

    void selectPane(const QString &id)
    {
        currentPane = id;
        controller->settings()->raw().setValue(kPaneSetting, id);
        if (titleBar) {
            titleBar.IsBackButtonVisible(id == kWhatsNewPane);
        }
        rebuildSidebar();
        rebuildPage();
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
        const QList<PageSnapshot> pages = model.pages();
        const bool returnable = whatsNewReturnPane == kShortcutPane
            || pageWithId(pages, whatsNewReturnPane);
        selectPane(returnable ? whatsNewReturnPane : pages.first().id);
    }

    void runAction(const QString &id)
    {
        if (id == QStringLiteral("whatsNew")) {
            showWhatsNew();
        }
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
            [this] {
                rebuildQueued = false;
                if (window) {
                    rebuildPage();
                    refreshBanner();
                }
            });
    }

    void rebuildPage()
    {
        if (!pageHost) {
            return;
        }
        const QList<PageSnapshot> pages = model.pages();
        const PageSnapshot *schemaPage =
            currentPane == kShortcutPane ? nullptr : pageWithId(pages, currentPane);
        if (currentPane != kShortcutPane && !schemaPage) {
            return;
        }
        double offset = 0;
        if (auto previous = pageHost.Child().try_as<ScrollViewer>()) {
            offset = previous.VerticalOffset();
        }
        UIElement page{nullptr};
        try {
            page = schemaPage ? buildPage(*schemaPage, host) : buildShortcutPage(host);
        } catch (const winrt::hresult_error &error) {
            // A throw from a dispatcher callback dies as a stowed exception
            // with no message anywhere; log it and keep the window alive.
            qWarning() << "settings pane" << currentPane << "failed to build:"
                       << QString::number(static_cast<quint32>(error.code()), 16)
                       << QString::fromWCharArray(error.message().c_str());
            return;
        }
        pageHost.Child(page);
        if (offset > 0) {
            if (auto scroll = page.try_as<ScrollViewer>()) {
                scroll.Loaded([offset](const IInspectable &sender, const auto &) {
                    sender.as<ScrollViewer>().ChangeView(nullptr, offset, nullptr, true);
                });
            }
        }
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
            if (currentPane == QStringLiteral("providers")) {
                rebuildPage();
            }
        }
    }

    // AppWindow::refreshUpdateBanner, drawn as an InfoBar: available /
    // downloading with progress / restart / restarting / error, plus the
    // What's New offer when nothing else is showing.
    void refreshBanner()
    {
        if (!banner) {
            return;
        }
        UpdateController *updates = controller->updates();
        const QString availableVersion = updates->availableVersion();
        if ((!availableVersion.isEmpty() && bannerVersion != availableVersion)
            || bannerInstalledVersion != updates->currentVersion()) {
            bannerDeferred = false;
            bannerVersion = availableVersion;
            bannerInstalledVersion = updates->currentVersion();
        }
        banner.Content(nullptr);
        bannerCloseAction = {};

        const auto action = [this](const QString &caption, std::function<void()> run) {
            Button button;
            button.Content(box_value(hs(caption)));
            button.Click([run = std::move(run)](const auto &, const auto &) { run(); });
            banner.ActionButton(button);
        };
        banner.ActionButton(nullptr);

        if (!updates->bannerVisible() && !controller->pendingWhatsNewVersion().isEmpty()) {
            banner.Severity(InfoBarSeverity::Success);
            banner.Message(hs(QStringLiteral("Speecher %1 is installed")
                                  .arg(updates->currentVersion().section(QLatin1Char('-'), 0, 0))));
            action(QStringLiteral("See what's new"), [this] { showWhatsNew(); });
            banner.IsClosable(true);
            bannerCloseAction = [this] { controller->clearPendingWhatsNew(); };
            banner.IsOpen(true);
            return;
        }
        // "Later" hides states where restart is not yet underway. Once
        // restarting has begun, keep its status visible to explain the exit.
        if (bannerDeferred
            && (updates->state() == UpdateController::State::ReadyToRestart
                || updates->state() == UpdateController::State::RestartPending)) {
            banner.IsOpen(false);
            return;
        }
        if (!updates->bannerVisible()) {
            banner.IsOpen(false);
            return;
        }
        banner.Severity(updates->state() == UpdateController::State::Error
                            ? InfoBarSeverity::Error
                            : InfoBarSeverity::Informational);
        banner.IsClosable(false);
        switch (updates->state()) {
        case UpdateController::State::UpdateAvailable:
            banner.Message(hs(
                updates->stableReplacementAvailable()
                    ? QStringLiteral("Switch to Stable Release %1 (replaces this Nightly Build)")
                          .arg(updates->availableVersion())
                    : QStringLiteral("Speecher %1 is available").arg(updates->availableVersion())));
            action(QStringLiteral("Update now"), [updates] { updates->updateNow(); });
            banner.IsClosable(true);
            bannerCloseAction = [updates] { updates->dismissAvailableVersion(); };
            break;
        case UpdateController::State::Downloading: {
            banner.Message(hs(QStringLiteral("Downloading Speecher %1")
                                  .arg(updates->availableVersion())));
            ProgressBar progress;
            progress.Minimum(0);
            progress.Maximum(100);
            progress.Value(updates->downloadPercent());
            banner.Content(progress);
            break;
        }
        case UpdateController::State::ReadyToRestart:
            banner.Message(hs(updates->errorMessage().isEmpty()
                                  ? QStringLiteral("Restart to finish updating")
                                  : updates->errorMessage()));
            action(QStringLiteral("Restart now"), [updates] { updates->updateNow(); });
            // The close button is "Later" here: hide until the next version.
            banner.IsClosable(true);
            bannerCloseAction = [this] {
                bannerDeferred = true;
                refreshBanner();
            };
            break;
        case UpdateController::State::RestartPending:
            banner.Message(L"Restarting after this dictation…");
            break;
        case UpdateController::State::Restarting:
            banner.Message(L"Restarting…");
            break;
        case UpdateController::State::Error:
            banner.Message(hs(updates->errorMessage()));
            // The Qt banner routes every caption through updateNow(), which
            // retries or opens the release page as the state demands.
            action(updates->manualInstallRequired() ? QStringLiteral("Open release page")
                                                    : QStringLiteral("Try again"),
                   [updates] { updates->updateNow(); });
            banner.IsClosable(true);
            bannerCloseAction = [updates] { updates->dismissAvailableVersion(); };
            break;
        default:
            banner.IsOpen(false);
            return;
        }
        banner.IsOpen(true);
    }

    bool capture(const QString &path)
    {
        if (!window) {
            return false;
        }
        const QString request = qEnvironmentVariable("SPEECHER_GRAB_PAGE")
                                    .toLower()
                                    .section(QLatin1Char(':'), 0, 0);
        if (request == kShortcutPane) {
            selectPane(kShortcutPane);
        } else {
            for (const PageSnapshot &page : model.pages()) {
                if (page.id.toLower() == request) {
                    selectPane(page.id);
                    break;
                }
            }
        }
        // Let composition catch up with the pane switch before printing.
        QEventLoop settle;
        QTimer::singleShot(250, &settle, &QEventLoop::quit);
        settle.exec();
        return printWindowTo(windowHandle(), path);
    }

    ApplicationController *controller;
    SettingsModel model;
    PaneHost host;
    std::function<void(const QString &)> actionHook;
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

    // Update banner state, as AppWindow keeps it.
    QString bannerVersion;
    QString bannerInstalledVersion;
    bool bannerDeferred = false;
    std::function<void()> bannerCloseAction;
};

SettingsWindow::SettingsWindow(ApplicationController *controller)
    : m_native(std::make_unique<Native>(controller))
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

bool SettingsWindow::capture(const QString &path)
{
    return m_native->capture(path);
}

void SettingsWindow::setActionHook(std::function<void(const QString &)> hook)
{
    m_native->actionHook = std::move(hook);
}

} // namespace speecher::win
