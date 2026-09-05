#include "frontend/win/DictationPanel.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "frontend/win/SettingsPage.h"

#include <windows.h>
#include <dwmapi.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <QTimer>

#include <algorithm>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;

constexpr int minimumWidth = 300;
constexpr int panelHeight = 52;
constexpr int previewChromeWidth = 190;
constexpr int screenEdgeMargin = 80;
constexpr int bottomMargin = 28;
constexpr auto windowClassName = L"SpeecherDictationPanel";

LRESULT CALLBACK panelWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DISPLAYCHANGE) {
        RECT rect{};
        POINT pointer{};
        GetWindowRect(window, &rect);
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = rect.right - rect.left;
        SetWindowPos(window, HWND_TOPMOST,
                     monitor.rcWork.left
                         + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
                     monitor.rcWork.bottom - (rect.bottom - rect.top) - bottomMargin,
                     0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

QString phaseGlyph(const QString &status, bool problem)
{
    if (problem) {
        return QString::fromUtf16(u"\uE7BA");
    }
    const QString phase = status.toLower();
    if (phase.isEmpty() || phase == QStringLiteral("preparing")
        || phase == QStringLiteral("starting")) {
        return QString::fromUtf16(u"\uE895");
    }
    if (phase == QStringLiteral("listening")) {
        return QString::fromUtf16(u"\uE720");
    }
    if (phase == QStringLiteral("stopping")) {
        return QString::fromUtf16(u"\uEC4F");
    }
    if (phase == QStringLiteral("refining")) {
        return QString::fromUtf16(u"\uE8A9");
    }
    return QString::fromUtf16(u"\uE724");
}

} // namespace

struct DictationPanel::Native : QObject {
    Native(ApplicationController *owner, DictationPanel *q)
        : QObject(q)
        , controller(owner)
        , panel(q)
    {
        DictationSession *session = controller->session();
        connect(session, &DictationSession::previewDisplayChanged, this,
                [this](const QString &text) { setPreview(text); });
        connect(session, &DictationSession::audioLevelChanged, this,
                [this](float value) { setLevel(value); });
        connect(session, &DictationSession::popupStatusChanged, this,
                [this](const QString &text) { setStatus(text); });
        connect(session, &DictationSession::popupShowRequested, this,
                [this](quint64 value) { show(value); });
        connect(session, &DictationSession::popupHideRequested, this, &Native::hide);
        connect(session, &DictationSession::popupFrozenChanged, this,
                [this](bool value) { frozen = value; });
        connect(session, &DictationSession::popupRefiningChanged, this,
                [this](bool value) { setRefining(value); });
        connect(session, &DictationSession::popupOAuthRefreshRequested, this, [this] {
            status = QStringLiteral("Refreshing sign-in...");
            preview = status;
            refresh();
        });
        connect(session, &DictationSession::popupListeningIndicatorRequested, this, [this] {
            setStatus(QStringLiteral("Listening"));
        });
        connect(session, &DictationSession::popupMessageRequested, this,
                [this](const QString &message) {
                    status = message;
                    completed = true;
                    refresh();
                });
        connect(session, &DictationSession::popupErrorRequested, this, &Native::showProblem);
    }

    ~Native() override
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
        windowClass.lpfnWndProc = panelWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = windowClassName;
        RegisterClassW(&windowClass);
        window = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            windowClassName, L"Speecher dictation", WS_POPUP,
            0, 0, minimumWidth, panelHeight, nullptr, nullptr,
            windowClass.hInstance, nullptr);

        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));

        source = DesktopWindowXamlSource();
        source.Initialize(Microsoft::UI::GetWindowIdFromWindow(window));

        Border chrome;
        chrome.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        chrome.Padding({20, 0, 20, 0});
        row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(12);
        row.VerticalAlignment(VerticalAlignment::Center);

        glyph = FontIcon();
        glyph.FontSize(20);
        row.Children().Append(glyph);

        text = TextBlock();
        text.VerticalAlignment(VerticalAlignment::Center);
        text.TextTrimming(TextTrimming::CharacterEllipsis);
        text.MaxLines(1);
        text.Width(240);
        row.Children().Append(text);

        level = ProgressBar();
        level.Width(96);
        level.Minimum(0);
        level.Maximum(1);
        level.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(level);

        ring = ProgressRing();
        ring.Width(24);
        ring.Height(24);
        ring.IsActive(true);
        ring.Visibility(Visibility::Collapsed);
        row.Children().Append(ring);

        dismiss = Button();
        dismiss.Content(box_value(L"Dismiss"));
        dismiss.Visibility(Visibility::Collapsed);
        dismiss.Click([this](const auto &, const auto &) {
            dismissProblem();
        });
        row.Children().Append(dismiss);

        chrome.Child(row);
        chrome.Loaded([this](const auto &, const auto &) {
            loaded = true;
            if (!pendingGeneration) {
                return;
            }
            const quint64 generation = pendingGeneration;
            QTimer::singleShot(0, panel, [this, generation] {
                presentedGeneration = generation;
                controller->session()->popupPresented(generation);
            });
        });
        source.Content(chrome);
        source.SystemBackdrop(DesktopAcrylicBackdrop());
        resize(minimumWidth);
    }

    void show(quint64 generation)
    {
        problem.clear();
        completed = false;
        pendingGeneration = generation;
        ensureWindow();
        refresh();
        reposition();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        if (loaded) {
            QTimer::singleShot(0, panel, [this, generation] {
                presentedGeneration = generation;
                controller->session()->popupPresented(generation);
            });
        }
    }

    void showProblem(const QString &message)
    {
        preview.clear();
        problem = message;
        pendingGeneration = 0;
        ensureWindow();
        refresh();
        reposition();
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }

    void hide()
    {
        if (window) {
            ShowWindow(window, SW_HIDE);
        }
    }

    void dismissProblem()
    {
        problem.clear();
        hide();
        controller->stopListening();
    }

    void setStatus(const QString &value)
    {
        status = value;
        refresh();
    }

    void setPreview(const QString &value)
    {
        if (frozen) {
            return;
        }
        preview = value.simplified();
        refresh();
    }

    void setLevel(float value)
    {
        ensureWindow();
        level.Value(std::clamp(value, 0.0f, 1.0f));
    }

    void setRefining(bool value)
    {
        refining = value;
        refresh();
    }

    void refresh()
    {
        if (!window) {
            return;
        }
        const bool hasProblem = !problem.isEmpty();
        // A finished delivery: the outcome message is the whole story, so the
        // spent preview words go and the icon and message centre in the pill.
        const bool finished = completed && !hasProblem;
        glyph.Glyph(hstring((refining && !hasProblem
                                 ? QString::fromUtf16(u"\uE8A9")
                                 : phaseGlyph(status, hasProblem))
                                .toStdWString()));
        QString shown = hasProblem ? problem
            : finished                ? status
            : preview.isEmpty()       ? status
                                      : preview;

        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        const int maximumWidth = std::max(
            minimumWidth, int(monitor.rcWork.right - monitor.rcWork.left) - screenEdgeMargin);
        const int wantedWidth = std::clamp(
            minimumWidth + std::max(0, int(shown.size()) - 32) * 7,
            minimumWidth, maximumWidth);
        const int textWidth = wantedWidth - previewChromeWidth;
        const int maximumCharacters = std::max(20, textWidth / 7);
        if (!hasProblem && shown.size() > maximumCharacters) {
            shown = QString::fromUtf16(u"\u2026") + shown.right(maximumCharacters - 1);
        }
        text.Text(hstring(shown.toStdWString()));
        if (finished) {
            text.ClearValue(FrameworkElement::WidthProperty());
            row.HorizontalAlignment(HorizontalAlignment::Center);
        } else {
            text.Width(textWidth);
            row.HorizontalAlignment(HorizontalAlignment::Left);
        }
        level.Visibility(!hasProblem && !refining
                                 && status.compare(QStringLiteral("listening"), Qt::CaseInsensitive) == 0
                             ? Visibility::Visible
                             : Visibility::Collapsed);
        ring.Visibility(!hasProblem && refining ? Visibility::Visible
                                                 : Visibility::Collapsed);
        dismiss.Visibility(hasProblem ? Visibility::Visible : Visibility::Collapsed);
        resize(wantedWidth);
        if (IsWindowVisible(window)) {
            reposition();
        }
    }

    void resize(int newWidth)
    {
        width = newWidth;
        if (source) {
            source.SiteBridge().MoveAndResize({0, 0, width, panelHeight});
        }
    }

    void reposition()
    {
        if (!window) {
            return;
        }
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        const int x = monitor.rcWork.left
            + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
        const int y = monitor.rcWork.bottom - panelHeight - bottomMargin;
        SetWindowPos(window, HWND_TOPMOST, x, y, width, panelHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    ApplicationController *controller;
    DictationPanel *panel;
    HWND window = nullptr;
    DesktopWindowXamlSource source{nullptr};
    FontIcon glyph{nullptr};
    TextBlock text{nullptr};
    ProgressBar level{nullptr};
    ProgressRing ring{nullptr};
    Button dismiss{nullptr};
    QString status;
    QString preview;
    QString problem;
    int width = minimumWidth;
    quint64 pendingGeneration = 0;
    quint64 presentedGeneration = 0;
    StackPanel row{nullptr};
    bool frozen = false;
    bool completed = false;
    bool refining = false;
    bool loaded = false;
};

DictationPanel::DictationPanel(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<Native>(controller, this))
{
}

DictationPanel::~DictationPanel() = default;

void DictationPanel::showProblem(const QString &message)
{
    m_native->showProblem(message);
}

void DictationPanel::showForTest(quint64 generation)
{
    m_native->show(generation);
}

void DictationPanel::dismissForTest()
{
    m_native->dismissProblem();
}

bool DictationPanel::visibleForTest() const
{
    return m_native->window && IsWindowVisible(m_native->window);
}

quint64 DictationPanel::presentedGenerationForTest() const
{
    return m_native->presentedGeneration;
}

qintptr DictationPanel::windowStyleForTest() const
{
    return m_native->window ? GetWindowLongPtrW(m_native->window, GWL_EXSTYLE) : 0;
}

} // namespace speecher
