#include "frontend/win/TrayFlyout.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/win/SettingsPage.h"

#include <windows.h>
#include <dwmapi.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;

constexpr int flyoutWidth = 300;
constexpr int flyoutHeight = 280;
constexpr auto windowClassName = L"SpeecherTrayFlyout";

LRESULT CALLBACK flyoutWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

Button textButton(const wchar_t *label)
{
    Button button;
    button.Content(box_value(label));
    button.HorizontalAlignment(HorizontalAlignment::Stretch);
    button.HorizontalContentAlignment(HorizontalAlignment::Center);
    return button;
}

} // namespace

struct TrayFlyout::Native {
    Native(ApplicationController *owner, TrayFlyout *q)
        : controller(owner)
        , flyout(q)
    {
        QObject::connect(controller, &ApplicationController::stateChanged, flyout,
                         [this](const QString &value) {
                             state = value;
                             refresh();
                         });
        QObject::connect(controller, &ApplicationController::audioLevelChanged, flyout,
                         [this](float value) {
                             if (level) {
                                 level.Value(std::clamp(value, 0.0f, 1.0f));
                             }
                         });
        QObject::connect(controller, &ApplicationController::transcriptDelivered, flyout,
                         [this](const QString &value) {
                             if (!value.isEmpty()) {
                                 lastTranscript = value;
                                 refresh();
                             }
                         });
        QObject::connect(controller, &ApplicationController::globalShortcutChanged, flyout,
                         [this] { refresh(); });
    }

    ~Native()
    {
        if (source) {
            source.Close();
        }
        if (window) {
            DestroyWindow(window);
        }
    }

    void ensureWindow()
    {
        if (window) {
            return;
        }
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = flyoutWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = windowClassName;
        RegisterClassW(&windowClass);
        window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST, windowClassName, L"Speecher",
            WS_POPUP, 0, 0, flyoutWidth, flyoutHeight,
            nullptr, nullptr, windowClass.hInstance, nullptr);

        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));

        source = DesktopWindowXamlSource();
        source.Initialize(Microsoft::UI::GetWindowIdFromWindow(window));
        source.SiteBridge().MoveAndResize({0, 0, flyoutWidth, flyoutHeight});

        StackPanel root;
        root.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        root.Padding({20, 20, 20, 20});
        root.Spacing(10);
        root.KeyDown([this](const auto &, const Input::KeyRoutedEventArgs &event) {
            if (event.Key() == Windows::System::VirtualKey::Escape) {
                hide();
                event.Handled(true);
            }
        });

        StackPanel heading;
        heading.Orientation(Orientation::Horizontal);
        heading.Spacing(10);
        statusGlyph = FontIcon();
        statusGlyph.Glyph(L"\uE720");
        statusGlyph.FontSize(20);
        statusText = TextBlock();
        statusText.Style(Application::Current().Resources()
                             .Lookup(box_value(L"BodyStrongTextBlockStyle"))
                             .as<Style>());
        statusText.VerticalAlignment(VerticalAlignment::Center);
        heading.Children().Append(statusGlyph);
        heading.Children().Append(statusText);
        root.Children().Append(heading);

        level = ProgressBar();
        level.Minimum(0);
        level.Maximum(1);
        root.Children().Append(level);

        toggle = textButton(L"Start Dictation");
        toggle.Style(Application::Current().Resources()
                         .Lookup(box_value(L"AccentButtonStyle"))
                         .as<Style>());
        toggle.Click([this](const auto &, const auto &) {
            hide();
            controller->toggle();
        });
        root.Children().Append(toggle);

        transcript = TextBlock();
        transcript.TextWrapping(TextWrapping::Wrap);
        transcript.TextTrimming(TextTrimming::CharacterEllipsis);
        transcript.MaxLines(3);
        transcript.Opacity(0.72);
        root.Children().Append(transcript);

        copy = textButton(L"Copy Transcript");
        copy.Click([this](const auto &, const auto &) {
            QGuiApplication::clipboard()->setText(lastTranscript);
        });
        root.Children().Append(copy);

        StackPanel shortcutRow;
        shortcutRow.Orientation(Orientation::Horizontal);
        shortcutRow.Spacing(12);
        TextBlock shortcutLabel;
        shortcutLabel.Text(L"Shortcut");
        shortcutLabel.Style(Application::Current().Resources()
                                .Lookup(box_value(L"BodyStrongTextBlockStyle"))
                                .as<Style>());
        shortcut = TextBlock();
        shortcut.Opacity(0.72);
        shortcutRow.Children().Append(shortcutLabel);
        shortcutRow.Children().Append(shortcut);
        root.Children().Append(shortcutRow);

        Button settings = textButton(L"Settings...");
        settings.Click([this](const auto &, const auto &) {
            hide();
            controller->showSettingsWindow();
        });
        root.Children().Append(settings);

        Button quit = textButton(L"Quit Speecher");
        quit.Click([this](const auto &, const auto &) {
            hide();
            controller->quitApplication();
        });
        root.Children().Append(quit);

        source.Content(root);
        source.SystemBackdrop(DesktopAcrylicBackdrop());
        refresh();
    }

    void refresh()
    {
        if (!window) {
            return;
        }
        const QString lowered = state.toLower();
        const bool active = lowered == QStringLiteral("starting")
            || lowered == QStringLiteral("listening");
        QString displayState = state;
        if (!displayState.isEmpty()) {
            displayState.replace(0, 1, displayState.left(1).toUpper());
        }
        statusText.Text(hstring((lowered.isEmpty() || lowered == QStringLiteral("idle")
                                     ? QStringLiteral("Speecher")
                                     : displayState)
                                    .toStdWString()));
        statusGlyph.Glyph(L"\uE720");
        level.Visibility(lowered == QStringLiteral("listening")
                             ? Visibility::Visible : Visibility::Collapsed);
        toggle.Content(box_value(active ? L"Stop Dictation" : L"Start Dictation"));
        transcript.Text(hstring((lastTranscript.isEmpty()
                                     ? QStringLiteral("Nothing dictated yet.")
                                     : lastTranscript)
                                    .toStdWString()));
        copy.Visibility(lastTranscript.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
        const QString shortcutText = controller->globalShortcutDisplay();
        shortcut.Text(hstring((shortcutText.isEmpty() ? QStringLiteral("None") : shortcutText)
                                  .toStdWString()));
    }

    void show(const RECT &iconRect)
    {
        ensureWindow();
        RECT anchor = iconRect;
        HMONITOR monitorHandle = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(monitorHandle, &monitor);
        int x = anchor.left + (anchor.right - anchor.left - flyoutWidth) / 2;
        int y = anchor.top - flyoutHeight - 8;
        x = std::clamp(x, int(monitor.rcWork.left), int(monitor.rcWork.right) - flyoutWidth);
        y = std::clamp(y, int(monitor.rcWork.top), int(monitor.rcWork.bottom) - flyoutHeight);
        SetWindowPos(window, HWND_TOPMOST, x, y, flyoutWidth, flyoutHeight,
                     SWP_SHOWWINDOW);
        SetForegroundWindow(window);
    }

    void hide()
    {
        if (window) {
            ShowWindow(window, SW_HIDE);
        }
    }

    ApplicationController *controller;
    TrayFlyout *flyout;
    HWND window = nullptr;
    DesktopWindowXamlSource source{nullptr};
    FontIcon statusGlyph{nullptr};
    TextBlock statusText{nullptr};
    ProgressBar level{nullptr};
    Button toggle{nullptr};
    TextBlock transcript{nullptr};
    Button copy{nullptr};
    TextBlock shortcut{nullptr};
    QString state;
    QString lastTranscript;
};

TrayFlyout::TrayFlyout(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<Native>(controller, this))
{
}

TrayFlyout::~TrayFlyout() = default;

void TrayFlyout::show(const tagRECT &iconRect)
{
    m_native->show(iconRect);
}

void TrayFlyout::hide()
{
    m_native->hide();
}

} // namespace speecher
