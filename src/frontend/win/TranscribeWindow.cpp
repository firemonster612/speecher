#include "frontend/win/TranscribeWindow.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/win/SettingsPage.h"
#include "frontend/win/TranscribePane.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>

#include <windows.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

// Non-ASCII text is written as \u escapes: MSVC reads a source without a BOM
// in the system code page.
constexpr const wchar_t *kTitle = L"Transcribe \u2014 Speecher";
// Device-independent pixels.
constexpr int kWidth = 560;
constexpr int kHeight = 680;

} // namespace

struct TranscribeWindow::Native {
    Native(ApplicationController *controller, TranscribePane *transcribe)
        : controller(controller)
        , transcribe(transcribe)
    {
        host.controller = controller;
        host.alive = alive;
        host.refresh = [this] { queueRebuild(); };
        host.hwnd = [this] { return windowHandle(); };
        host.xamlRoot = [this] { return root ? root.XamlRoot() : XamlRoot{nullptr}; };
        host.effectiveTheme = [this] { return root ? root.ActualTheme() : ElementTheme::Default; };
    }

    ~Native()
    {
        // Dispatcher callbacks queued by this object can still be pending;
        // they hold a weak copy of this token and bail once it is cleared.
        *alive = false;
        transcribe->forget(host);
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
        if (!window) {
            transcribe->enter();
            createWindow();
        }
        window.Activate();
        AllowSetForegroundWindow(ASFW_ANY);
        SetForegroundWindow(windowHandle());
    }

    void createWindow()
    {
        window = Window();
        window.SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
        window.ExtendsContentIntoTitleBar(true);
        window.Title(kTitle);

        root = Grid();
        RowDefinition titleRow;
        titleRow.Height({0, GridUnitType::Auto});
        RowDefinition contentRow;
        contentRow.Height({1, GridUnitType::Star});
        root.RowDefinitions().Append(titleRow);
        root.RowDefinitions().Append(contentRow);
        root.RequestedTheme(requestedTheme(controller->settings()->theme()));
        // The code-resolved secondary brushes follow the theme only through a
        // rebuild; this covers the system flipping while set to System.
        root.ActualThemeChanged([this](const auto &, const auto &) { queueRebuild(); });

        TitleBar titleBar;
        titleBar.Title(kTitle);
        titleBar.IsBackButtonVisible(false);
        setWindowIcon(window, titleBar);
        root.Children().Append(titleBar);

        pageHost = Border();
        Grid::SetRow(pageHost, 1);
        root.Children().Append(pageHost);

        window.Content(root);
        window.SetTitleBar(titleBar);
        closedToken = window.Closed([this](const auto &, const auto &) { windowClosed(); });
        placeWindow();
        rebuild();
    }

    // Centred on the cursor's monitor. The window lands there first so its
    // DPI is that monitor's: the size is in DIPs and SetWindowPos wants
    // physical pixels.
    void placeWindow()
    {
        const HWND handle = windowHandle();
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor)) {
            return;
        }
        const RECT area = monitor.rcWork;
        SetWindowPos(handle, nullptr, area.left, area.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        const double scale = GetDpiForWindow(handle) / 96.0;
        const int width = std::min(int(kWidth * scale + 0.5), int(area.right - area.left));
        const int height = std::min(int(kHeight * scale + 0.5), int(area.bottom - area.top));
        SetWindowPos(handle, nullptr, area.left + (area.right - area.left - width) / 2,
                     area.top + (area.bottom - area.top - height) / 2, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void windowClosed()
    {
        transcribe->forget(host);
        window = nullptr;
        root = nullptr;
        pageHost = nullptr;
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
                rebuild();
            });
    }

    void rebuild()
    {
        if (!pageHost) {
            return;
        }
        try {
            replacePage(pageHost, transcribe->build(host, {}));
        } catch (const winrt::hresult_error &error) {
            // A throw from a dispatcher callback dies as a stowed exception
            // with no message anywhere; log it and keep the window alive.
            qWarning() << "Transcribe window failed to build:"
                       << QString::number(static_cast<quint32>(error.code()), 16)
                       << QString::fromWCharArray(error.message().c_str());
        }
    }

    bool capture(const QString &path)
    {
        show();
        // Let composition catch up with the first frame before printing.
        QEventLoop settle;
        QTimer::singleShot(250, &settle, &QEventLoop::quit);
        settle.exec();
        return printWindowTo(windowHandle(), path);
    }

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    ApplicationController *controller;
    TranscribePane *transcribe;
    PaneHost host;

    Window window{nullptr};
    winrt::event_token closedToken{};
    Grid root{nullptr};
    Border pageHost{nullptr};
    bool rebuildQueued = false;
};

TranscribeWindow::TranscribeWindow(ApplicationController *controller, TranscribePane *transcribe)
    : m_native(std::make_unique<Native>(controller, transcribe))
{
}

TranscribeWindow::~TranscribeWindow() = default;

void TranscribeWindow::show()
{
    m_native->show();
}

bool TranscribeWindow::capture(const QString &path)
{
    return m_native->capture(path);
}

} // namespace speecher::win
