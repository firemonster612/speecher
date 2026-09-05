#include "frontend/win/WinFrontEnd.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "frontend/win/DictationPanel.h"
#include "frontend/win/SettingsWindow.h"
#include "frontend/win/SetupWindow.h"
#include "frontend/win/TrayIcon.h"
#include "frontend/win/WinUiHost.h"

#include <windows.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <QImage>
#include <QTimer>

#include <utility>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

NavigationViewItem navigationItem(const wchar_t *label, const wchar_t *tag)
{
    NavigationViewItem item;
    item.Content(box_value(label));
    item.Tag(box_value(tag));
    return item;
}

} // namespace

// This is intentionally only a hosting stub. W2 replaces its body with the
// schema renderer while keeping SettingsWindow.h's interface.
struct SettingsWindow::Native {
    explicit Native(ApplicationController *owner)
        : controller(owner)
    {
    }

    ~Native()
    {
        if (window) {
            window.Close();
        }
    }

    void ensureWindow()
    {
        if (window) {
            return;
        }
        window = Window();
        window.SystemBackdrop(MicaBackdrop());
        window.ExtendsContentIntoTitleBar(true);
        window.Closed([this](const auto &, const auto &) { window = nullptr; });

        Grid root;
        TitleBar titleBar;
        titleBar.Title(L"Speecher");
        titleBar.IsBackButtonVisible(false);
        titleBar.VerticalAlignment(VerticalAlignment::Top);

        navigation = NavigationView();
        navigation.Margin({0, 48, 0, 0});
        navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navigation.IsPaneToggleButtonVisible(false);
        navigation.IsSettingsVisible(false);
        AutoSuggestBox search;
        search.PlaceholderText(L"Find a setting");
        navigation.AutoSuggestBox(search);
        navigation.MenuItems().Append(navigationItem(L"General", L"general"));
        navigation.MenuItems().Append(navigationItem(L"Dictation", L"dictation"));
        navigation.MenuItems().Append(navigationItem(L"Shortcut", L"shortcut"));
        navigation.MenuItems().Append(navigationItem(L"Text", L"text"));
        navigation.MenuItems().Append(navigationItem(L"Delivery", L"delivery"));
        navigation.MenuItems().Append(navigationItem(L"Apps", L"apps"));
        navigation.MenuItems().Append(navigationItem(L"Vocabulary", L"vocabulary"));
        navigation.MenuItems().Append(navigationItem(L"Accounts", L"accounts"));

        pageContent = StackPanel();
        pageContent.Spacing(16);
        pageContent.Margin({36, 28, 36, 36});
        navigation.Content(pageContent);
        root.Children().Append(navigation);
        root.Children().Append(titleBar);
        window.Content(root);
        window.SetTitleBar(titleBar);

        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        SetWindowTextW(handle, L"Speecher");
        SetWindowPos(handle, nullptr, 0, 0, 960, 680,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        renderPage();
    }

    void renderPage()
    {
        pageContent.Children().Clear();
        TextBlock title;
        title.Text(hstring((pane == QStringLiteral("whatsNew")
                                ? QStringLiteral("What's New")
                                : QStringLiteral("Speecher settings"))
                               .toStdWString()));
        title.Style(Application::Current().Resources()
                        .Lookup(box_value(L"TitleTextBlockStyle"))
                        .as<Style>());
        pageContent.Children().Append(title);
        TextBlock note;
        note.Text(L"The native settings pages are supplied by the W2 settings branch.");
        note.TextWrapping(TextWrapping::Wrap);
        pageContent.Children().Append(note);
    }

    void show()
    {
        ensureWindow();
        window.Activate();
        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        AllowSetForegroundWindow(ASFW_ANY);
        SetForegroundWindow(handle);
    }

    bool capture(const QString &path)
    {
        if (!window) {
            return false;
        }
        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        RECT rect{};
        if (!GetWindowRect(handle, &rect)) {
            return false;
        }
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        HDC windowDc = GetWindowDC(handle);
        HDC memoryDc = CreateCompatibleDC(windowDc);
        HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height);
        HGDIOBJ previous = SelectObject(memoryDc, bitmap);
        const BOOL printed = PrintWindow(handle, memoryDc, PW_RENDERFULLCONTENT);
        QImage image(width, height, QImage::Format_ARGB32);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const int copied = printed
            ? GetDIBits(memoryDc, bitmap, 0, height, image.bits(), &info, DIB_RGB_COLORS)
            : 0;
        SelectObject(memoryDc, previous);
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(handle, windowDc);
        return copied == height && image.save(path);
    }

    ApplicationController *controller;
    Window window{nullptr};
    NavigationView navigation{nullptr};
    StackPanel pageContent{nullptr};
    QString pane = QStringLiteral("general");
    std::function<void(QString)> actionHandler;
};

SettingsWindow::SettingsWindow(ApplicationController *controller)
    : m_native(std::make_unique<Native>(controller))
{
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::show()
{
    m_native->show();
}

void SettingsWindow::navigate(const QString &paneId)
{
    m_native->pane = paneId;
    if (m_native->window) {
        m_native->renderPage();
    }
}

bool SettingsWindow::capture(const QString &path)
{
    return m_native->capture(path);
}

void SettingsWindow::setActionHandler(std::function<void(QString)> handler)
{
    m_native->actionHandler = std::move(handler);
}

struct WinFrontEnd::Native {
    Native(ApplicationController *owner,
           WinFrontEnd *q,
           std::unique_ptr<WinUiHost> winUiHost)
        : controller(owner)
        , frontEnd(q)
        , host(std::move(winUiHost))
    {
        panel = std::make_unique<DictationPanel>(controller, frontEnd);
        tray = std::make_unique<TrayIcon>(
            controller, [q] { q->showSettingsWindow(); }, frontEnd);
        trayReady = new QTimer(frontEnd);
        trayReady->setSingleShot(true);
        trayReady->setInterval(50);
        QObject::connect(trayReady, &QTimer::timeout, frontEnd, &WinFrontEnd::reportReady);
        trayReady->start();
    }

    ~Native()
    {
        setup.reset();
        settings.reset();
        tray.reset();
        panel.reset();
        host->shutdown();
    }

    SettingsWindow *settingsWindow()
    {
        if (!settings) {
            settings = std::make_unique<SettingsWindow>(controller);
            settings->setActionHandler(
                [q = frontEnd](QString id) { q->actionTriggered(id); });
        }
        return settings.get();
    }

    ApplicationController *controller;
    WinFrontEnd *frontEnd;
    std::unique_ptr<WinUiHost> host;
    std::unique_ptr<DictationPanel> panel;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<SettingsWindow> settings;
    std::unique_ptr<SetupWindow> setup;
    QTimer *trayReady = nullptr;
    bool ready = false;
};

WinFrontEnd::WinFrontEnd(ApplicationController *controller,
                         std::unique_ptr<WinUiHost> host)
    : QObject(controller)
    , m_controller(controller)
    , m_native(std::make_unique<Native>(controller, this, std::move(host)))
{
}

WinFrontEnd::~WinFrontEnd() = default;

void WinFrontEnd::showMainWindow()
{
    showSettingsWindow();
}

void WinFrontEnd::showSettingsWindow()
{
    m_native->trayReady->stop();
    m_native->settingsWindow()->show();
    QTimer::singleShot(0, this, &WinFrontEnd::reportReady);
}

void WinFrontEnd::showSetupAssistant(SetupAssistantPage page)
{
    m_native->trayReady->stop();
    if (!m_native->setup) {
        m_native->setup = std::make_unique<SetupWindow>(
            m_controller, [this] { reportReady(); }, this);
    }
    m_native->setup->show(page);
}

bool WinFrontEnd::captureMainWindow(const QString &path)
{
    return m_native->settings && m_native->settings->capture(path);
}

void WinFrontEnd::showDictationError(const QString &message)
{
    m_native->panel->showProblem(message);
}

void WinFrontEnd::alert()
{
    MessageBeep(MB_OK);
}

void WinFrontEnd::showPanelForTest(quint64 generation)
{
    m_native->panel->showForTest(generation);
}

void WinFrontEnd::dismissPanelForTest()
{
    m_native->panel->dismissForTest();
}

bool WinFrontEnd::panelVisibleForTest() const
{
    return m_native->panel->visibleForTest();
}

quint64 WinFrontEnd::panelPresentedGenerationForTest() const
{
    return m_native->panel->presentedGenerationForTest();
}

qintptr WinFrontEnd::panelWindowStyleForTest() const
{
    return m_native->panel->windowStyleForTest();
}

void WinFrontEnd::actionTriggered(const QString &rowId)
{
    if (rowId == QStringLiteral("runSetup")) {
        m_controller->showSetupAssistant();
    } else if (rowId == QStringLiteral("checkForUpdates")) {
        if (m_controller->updates()->state() == UpdateController::State::UpdateAvailable) {
            m_controller->updates()->updateNow();
        } else {
            m_controller->updates()->checkForUpdates(m_controller->settings()->updateChannel());
        }
    } else if (rowId == QStringLiteral("whatsNew")) {
        m_native->settingsWindow()->navigate(QStringLiteral("whatsNew"));
        m_controller->clearPendingWhatsNew();
    }
    // Windows UI Automation has no consent switch, so enableAccessibility is
    // deliberately a no-op.
}

void WinFrontEnd::reportReady()
{
    if (m_native->ready) {
        return;
    }
    m_native->ready = true;
    m_controller->frontEndReady();
}

} // namespace speecher
