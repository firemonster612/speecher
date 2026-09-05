#include "frontend/win/WinFrontEnd.h"

#include "app/ApplicationController.h"
#include "frontend/win/SettingsWindow.h"

#include <windows.h>
#include <dwmapi.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
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

} // namespace

struct WinFrontEnd::Native {
    explicit Native(ApplicationController *controller)
        : settingsWindow(controller)
    {
    }

    ~Native()
    {
        if (pillSource) {
            pillSource.Close();
        }
        if (pillWindow) {
            DestroyWindow(pillWindow);
        }
    }

    // W4 replaces this spike pill with the real DictationPanel.
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

    win::SettingsWindow settingsWindow;
    HWND pillWindow = nullptr;
    DesktopWindowXamlSource pillSource{nullptr};
    TextBlock pillText{nullptr};
};

WinFrontEnd::WinFrontEnd(ApplicationController *controller)
    : m_controller(controller)
    , m_native(std::make_unique<Native>(controller))
{
}

WinFrontEnd::~WinFrontEnd() = default;

void WinFrontEnd::showMainWindow()
{
    m_native->settingsWindow.show();
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
    return m_native->settingsWindow.capture(path);
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
