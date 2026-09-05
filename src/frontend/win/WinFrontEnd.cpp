#include "frontend/win/WinFrontEnd.h"

#include "app/ApplicationController.h"

#include <windows.h>
#include <dwmapi.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <QTimer>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;

constexpr int pillWidth = 420;
constexpr int pillHeight = 64;
constexpr int pillBottomMargin = 28;
constexpr auto pillClassName = L"SpeecherWinUiPill";

LRESULT CALLBACK pillWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

NavigationViewItem navigationItem(const wchar_t *label)
{
    NavigationViewItem item;
    item.Content(box_value(label));
    return item;
}

} // namespace

struct WinFrontEnd::Native {
    ~Native()
    {
        if (pillSource) {
            pillSource.Close();
        }
        if (pillWindow) {
            DestroyWindow(pillWindow);
        }
        if (mainWindow) {
            mainWindow.Close();
        }
    }

    void createMainWindow()
    {
        mainWindow = Window();
        mainWindow.Closed([this](const auto &, const auto &) { mainWindow = nullptr; });
        mainWindow.SystemBackdrop(MicaBackdrop());
        mainWindow.ExtendsContentIntoTitleBar(true);

        Grid root;
        TitleBar titleBar;
        titleBar.VerticalAlignment(VerticalAlignment::Top);
        titleBar.Title(L"Speecher");
        titleBar.IsBackButtonVisible(false);

        NavigationView navigation;
        navigation.Margin({0, 48, 0, 0});
        navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navigation.IsPaneToggleButtonVisible(false);
        AutoSuggestBox search;
        search.PlaceholderText(L"Search");
        navigation.AutoSuggestBox(search);
        navigation.MenuItems().Append(navigationItem(L"General"));
        navigation.MenuItems().Append(navigationItem(L"Dictation"));
        navigation.MenuItems().Append(navigationItem(L"About"));

        StackPanel content;
        content.Spacing(16);
        content.Margin({36, 28, 36, 36});
        TextBlock pageTitle;
        pageTitle.Text(L"Windows hosting spike");
        pageTitle.Style(Application::Current().Resources()
                            .Lookup(box_value(L"TitleTextBlockStyle"))
                            .as<Style>());
        content.Children().Append(pageTitle);

        ToggleSwitch toggle;
        toggle.Header(box_value(L"Enable dictation"));
        content.Children().Append(toggle);

        ComboBox comboBox;
        comboBox.Header(box_value(L"Microphone"));
        comboBox.Items().Append(box_value(L"Default microphone"));
        comboBox.Items().Append(box_value(L"External microphone"));
        comboBox.SelectedIndex(0);
        content.Children().Append(comboBox);

        TextBox textBox;
        textBox.Header(box_value(L"Keyboard input"));
        textBox.PlaceholderText(L"Type here to test input routing");
        content.Children().Append(textBox);

        navigation.Content(content);
        root.Children().Append(navigation);
        root.Children().Append(titleBar);
        mainWindow.Content(root);
        mainWindow.SetTitleBar(titleBar);
        mainWindow.Activate();
        HWND handle = nullptr;
        mainWindow.as<::IWindowNative>()->get_WindowHandle(&handle);
        SetWindowPos(handle, nullptr, 0, 0, 960, 680,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void showPill(const QString &message)
    {
        const HWND foregroundWindow = GetForegroundWindow();
        if (!pillWindow) {
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = pillWindowProc;
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = pillClassName;
            RegisterClassW(&windowClass);
            pillWindow = CreateWindowExW(
                WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                pillClassName,
                L"Speecher",
                WS_POPUP,
                0,
                0,
                pillWidth,
                pillHeight,
                nullptr,
                nullptr,
                windowClass.hInstance,
                nullptr);

            const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(pillWindow,
                                  DWMWA_WINDOW_CORNER_PREFERENCE,
                                  &corner,
                                  sizeof(corner));

            pillSource = DesktopWindowXamlSource();
            pillSource.Initialize(Microsoft::UI::GetWindowIdFromWindow(pillWindow));

            Border border;
            border.Padding({18, 0, 18, 0});
            pillText = TextBlock();
            pillText.VerticalAlignment(VerticalAlignment::Center);
            pillText.TextTrimming(TextTrimming::CharacterEllipsis);
            border.Child(pillText);
            pillSource.Content(border);
            pillSource.SystemBackdrop(DesktopAcrylicBackdrop());
            pillSource.SiteBridge().MoveAndResize({0, 0, pillWidth, pillHeight});
        }

        pillText.Text(winrt::hstring(message.toStdWString()));
        MONITORINFO monitorInfo{sizeof(monitorInfo)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
        const int x = monitorInfo.rcWork.left
            + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - pillWidth) / 2;
        const int y = monitorInfo.rcWork.bottom - pillHeight - pillBottomMargin;
        SetWindowPos(pillWindow,
                     HWND_TOPMOST,
                     x,
                     y,
                     pillWidth,
                     pillHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        QTimer::singleShot(0, [foregroundWindow] {
            if (foregroundWindow) {
                SetForegroundWindow(foregroundWindow);
            }
        });
    }

    Window mainWindow{nullptr};
    HWND pillWindow = nullptr;
    DesktopWindowXamlSource pillSource{nullptr};
    TextBlock pillText{nullptr};
};

WinFrontEnd::WinFrontEnd(ApplicationController *controller)
    : m_controller(controller)
    , m_native(std::make_unique<Native>())
{
}

WinFrontEnd::~WinFrontEnd() = default;

void WinFrontEnd::showMainWindow()
{
    if (!m_native->mainWindow) {
        m_native->createMainWindow();
    } else {
        m_native->mainWindow.Activate();
    }
    QTimer::singleShot(0, m_controller, &ApplicationController::frontEndReady);
}

void WinFrontEnd::showSettingsWindow()
{
    showMainWindow();
}

void WinFrontEnd::showSetupAssistant(SetupAssistantPage page)
{
    Q_UNUSED(page);
    showMainWindow();
}

bool WinFrontEnd::captureMainWindow(const QString &path)
{
    Q_UNUSED(path);
    return false;
}

void WinFrontEnd::showDictationError(const QString &message)
{
    m_native->showPill(message);
}

void WinFrontEnd::alert()
{
    MessageBeep(MB_OK);
}

} // namespace speecher
