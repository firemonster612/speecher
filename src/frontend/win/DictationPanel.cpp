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
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#pragma pop_macro("GetCurrentTime")

#include <QImage>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;

// The panel's layout constants are DIPs, as the XAML content measures them;
// every HWND move and resize scales them by the window's DPI.
constexpr int panelWidth = 420;
constexpr int panelHeight = 52;
constexpr int previewChromeWidth = 190;
constexpr int screenEdgeMargin = 80;
constexpr int bottomMargin = 28;
constexpr int bannerGap = 12;
constexpr auto windowClassName = L"SpeecherDictationPanel";

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

win::UpdateChipState win::updateChipState(UpdateController::State state, const QString &version,
                                          int percent, const QString &error, bool repeatedFailure,
                                          bool manualInstall, DictationState sessionState)
{
    const bool canAct = sessionState == DictationState::Idle
        || sessionState == DictationState::Error;
    using State = UpdateController::State;
    switch (state) {
    case State::UpdateAvailable:
        // The banner carries the base version; a nightly's -suffix is noise here.
        return {QStringLiteral("Speecher %1 available")
                    .arg(version.section(QLatin1Char('-'), 0, 0)),
                QStringLiteral("Install and restart"), true, true};
    case State::Downloading:
        return {QStringLiteral("Downloading %1%").arg(percent), {}, true, false};
    case State::ReadyToRestart:
        return {error.isEmpty() ? QStringLiteral("Update ready") : error,
                QStringLiteral("Restart now"), true, true};
    case State::RestartPending:
        return {QStringLiteral("Restarting after this dictation…"), {}, true, false};
    case State::Restarting:
        return {QStringLiteral("Restarting…"), {}, true, false};
    case State::Error:
        return {error,
                manualInstall ? QStringLiteral("Open release page")
                              : QStringLiteral("Try again"),
                true, canAct};
    case State::CheckFailed:
        if (repeatedFailure) {
            return {QStringLiteral("Update check failed"), QStringLiteral("Try again"),
                    true, canAct};
        }
        return {};
    default:
        return {};
    }
}

struct DictationPanel::Native : QObject {
    enum class Phase { Live, Transcribing, Refining };

    Native(ApplicationController *owner, DictationPanel *q)
        : QObject(q)
        , controller(owner)
        , panel(q)
    {
        whatsNewAutoHide.setSingleShot(true);
        whatsNewAutoHide.setInterval(6000);
        connect(&whatsNewAutoHide, &QTimer::timeout, this, [this] {
            whatsNewHidden = true;
            refresh();
        });
        connect(controller->updates(), &UpdateController::changed, this, &Native::refresh);
        connect(controller, &ApplicationController::whatsNewChanged, this, [this] {
            whatsNewHidden = false;
            if (window && IsWindowVisible(window)) {
                whatsNewAutoHide.start();
            }
            refresh();
        });
        DictationSession *session = controller->session();
        connect(session, &DictationSession::stateChanged, this, &Native::refresh);
        connect(session, &DictationSession::previewDisplayChanged, this,
                [this](const QString &text) { setPreview(text); });
        connect(session, &DictationSession::popupRefinementPreviewChanged, this,
                [this](const QString &value) {
                    if (phase != Phase::Refining) {
                        return;
                    }
                    preview = value.simplified();
                    refresh();
                });
        connect(session, &DictationSession::audioLevelChanged, this,
                [this](float value) { setLevel(value); });
        connect(session, &DictationSession::popupStatusChanged, this,
                [this](const QString &text) { setStatus(text); });
        connect(session, &DictationSession::popupShowRequested, this,
                [this](quint64 value) { show(value); });
        connect(session, &DictationSession::popupHideRequested, this, &Native::hide);
        connect(session, &DictationSession::popupFrozenChanged, this,
                [this](bool value) {
                    frozen = value;
                    // Unfreezing at session start returns to the live phase,
                    // like the Qt and mac panels, so a preview clear emitted
                    // before show() is never dropped by a stale phase.
                    if (!value) {
                        phase = Phase::Live;
                    }
                });
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
        if (bannerSource) {
            bannerSource.Close();
        }
        if (banner) {
            DestroyWindow(banner);
        }
        if (source) {
            source.Close();
        }
        if (window) {
            DestroyWindow(window);
        }
    }

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED) {
            auto *native = reinterpret_cast<Native *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (native) {
                // Re-derive the physical size and position at the new DPI or
                // monitor layout; refresh alone only repositions on a width
                // change.
                QTimer::singleShot(0, native, [native] {
                    native->refresh();
                    native->resize(native->width);
                    if (IsWindowVisible(native->window)) {
                        native->reposition();
                    }
                });
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void ensureWindow()
    {
        if (window) {
            return;
        }
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = windowClassName;
        RegisterClassW(&windowClass);
        window = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            windowClassName, L"Speecher dictation", WS_POPUP,
            0, 0, panelWidth, panelHeight, nullptr, nullptr,
            windowClass.hInstance, this);

        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));

        source = DesktopWindowXamlSource();
        source.Initialize(Microsoft::UI::GetWindowIdFromWindow(window));

        chrome = Border();
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
        text.Width(panelWidth - previewChromeWidth);
        row.Children().Append(text);
        probe = TextBlock();

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
        resize(panelWidth);
    }

    // The notices live in their own rounded acrylic surface floating above
    // the pill, never inside the pill's slab: each one is a plain message
    // with an explicitly labelled accent button beside it, so the action
    // reads as a button rather than asking the user to guess that colored
    // text is clickable.
    void ensureBanner()
    {
        if (banner) {
            return;
        }
        banner = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            windowClassName, L"Speecher notices", WS_POPUP,
            0, 0, panelWidth, panelHeight, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(banner, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));

        bannerSource = DesktopWindowXamlSource();
        bannerSource.Initialize(Microsoft::UI::GetWindowIdFromWindow(banner));

        const auto accentStyle = Application::Current().Resources()
                                     .Lookup(box_value(hstring(L"AccentButtonStyle")))
                                     .as<Microsoft::UI::Xaml::Style>();
        const auto makeRow = [&accentStyle](TextBlock &message, Button &action) {
            StackPanel row;
            row.Orientation(Orientation::Horizontal);
            row.HorizontalAlignment(HorizontalAlignment::Center);
            row.Spacing(10);
            message = TextBlock();
            message.VerticalAlignment(VerticalAlignment::Center);
            message.TextWrapping(TextWrapping::Wrap);
            row.Children().Append(message);
            action = Button();
            action.Style(accentStyle);
            action.CornerRadius({8, 8, 8, 8});
            action.Padding({14, 6, 14, 6});
            row.Children().Append(action);
            return row;
        };

        bannerRoot = StackPanel();
        bannerRoot.Spacing(8);
        bannerRoot.Padding({16, 10, 16, 10});
        whatsNewRow = makeRow(whatsNewText, whatsNewAction);
        whatsNewAction.Content(box_value(L"See what's new"));
        whatsNewAction.Click([this](const auto &, const auto &) {
            emit panel->whatsNewRequested();
        });
        Button whatsNewDismiss;
        whatsNewDismiss.Width(28);
        whatsNewDismiss.Height(28);
        whatsNewDismiss.Padding({0, 0, 0, 0});
        whatsNewDismiss.CornerRadius({14, 14, 14, 14});
        FontIcon closeIcon;
        closeIcon.Glyph(L"");
        closeIcon.FontSize(10);
        whatsNewDismiss.Content(closeIcon);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            whatsNewDismiss, L"Dismiss what's new");
        whatsNewDismiss.Click([this](const auto &, const auto &) {
            controller->clearPendingWhatsNew();
        });
        whatsNewRow.Children().Append(whatsNewDismiss);
        bannerRoot.Children().Append(whatsNewRow);
        updateRow = makeRow(updateText, updateAction);
        updateAction.Click([this](const auto &, const auto &) {
            controller->updates()->installAndRestart();
        });
        bannerRoot.Children().Append(updateRow);
        bannerSource.Content(bannerRoot);
        bannerSource.SystemBackdrop(DesktopAcrylicBackdrop());
    }

    void refreshBanner()
    {
        auto *updates = controller->updates();
        const auto notice = win::updateChipState(
            updates->state(), updates->availableVersion(), updates->downloadPercent(),
            updates->errorMessage(), updates->repeatedAutomaticCheckFailure(),
            updates->manualInstallRequired(), controller->session()->state());
        const bool showWhatsNew = !whatsNewHidden
            && !controller->pendingWhatsNewVersion().isEmpty();
        if (!window || !IsWindowVisible(window) || (!notice.visible && !showWhatsNew)) {
            if (banner) {
                ShowWindow(banner, SW_HIDE);
            }
            return;
        }
        ensureBanner();
        bannerRoot.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        updateText.Text(hstring(notice.text.toStdWString()));
        updateAction.Content(box_value(hstring(notice.action.toStdWString())));
        updateAction.Visibility(notice.action.isEmpty() ? Visibility::Collapsed
                                                        : Visibility::Visible);
        updateAction.IsEnabled(notice.enabled);
        updateRow.Visibility(notice.visible ? Visibility::Visible : Visibility::Collapsed);
        whatsNewText.Text(hstring(
            QStringLiteral("Speecher %1 installed")
                .arg(updates->currentVersion().section(QLatin1Char('-'), 0, 0))
                .toStdWString()));
        whatsNewRow.Visibility(showWhatsNew ? Visibility::Visible : Visibility::Collapsed);
        positionBanner();
        ShowWindow(banner, SW_SHOWNOACTIVATE);
    }

    // Centered above the pill with a small gap, sized to the measured rows.
    void positionBanner()
    {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        const float maximumWidth = float(
            (monitor.rcWork.right - monitor.rcWork.left) / scale() - screenEdgeMargin);
        bannerRoot.Measure({maximumWidth, std::numeric_limits<float>::infinity()});
        const int bannerWidth = px(int(std::ceil(bannerRoot.DesiredSize().Width)));
        const int bannerHeight = px(int(std::ceil(bannerRoot.DesiredSize().Height)));
        RECT pill{};
        GetWindowRect(window, &pill);
        const int x = pill.left + (pill.right - pill.left - bannerWidth) / 2;
        const int y = pill.top - bannerHeight - px(bannerGap);
        SetWindowPos(banner, HWND_TOPMOST, x, y, bannerWidth, bannerHeight,
                     SWP_NOACTIVATE);
        bannerSource.SiteBridge().MoveAndResize({0, 0, bannerWidth, bannerHeight});
    }

    void show(quint64 generation)
    {
        problem.clear();
        // The previous dictation's words are spent; the session's clearing
        // preview can be dropped by the frozen guard, so clear here too.
        preview.clear();
        completed = false;
        phase = Phase::Live;
        pendingGeneration = generation;
        ensureWindow();
        applyTheme();
        // Each dictation starts back at the floor, like the mac panel's
        // empty-preview reset, instead of inheriting the last one's width.
        resize(panelWidth);
        whatsNewHidden = false;
        refresh();
        reposition();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        refreshBanner();
        if (!controller->pendingWhatsNewVersion().isEmpty()) {
            whatsNewAutoHide.start();
        }
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
        applyTheme();
        whatsNewHidden = false;
        refresh();
        reposition();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        refreshBanner();
        if (!controller->pendingWhatsNewVersion().isEmpty()) {
            whatsNewAutoHide.start();
        }
    }

    void hide()
    {
        whatsNewAutoHide.stop();
        setShimmer(false);
        if (banner) {
            ShowWindow(banner, SW_HIDE);
        }
        if (window) {
            ShowWindow(window, SW_HIDE);
        }
    }

    // The window and its XAML tree outlive a theme change in Settings; the
    // captured RequestedTheme has to follow it on the next showing.
    void applyTheme()
    {
        if (chrome) {
            chrome.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
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
        if (value.compare(QStringLiteral("stopping"), Qt::CaseInsensitive) == 0) {
            phase = Phase::Transcribing;
            preview.clear();
        }
        refresh();
    }

    void setPreview(const QString &value)
    {
        if (phase != Phase::Live || frozen) {
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
        if (value) {
            phase = Phase::Refining;
            preview.clear();
        }
        refresh();
    }

    void setShimmer(bool active)
    {
        if (active == shimmering) {
            return;
        }
        shimmering = active;
        if (!active) {
            shimmer.Stop();
            text.Foreground(normalForeground);
            return;
        }
        using namespace Microsoft::UI::Xaml::Media::Animation;
        normalForeground = text.Foreground();
        // High-contrast themes can hand out a non-solid foreground brush.
        const auto solid = normalForeground.try_as<SolidColorBrush>();
        const auto color = solid ? solid.Color()
                                 : winrt::Windows::UI::Color{255, 128, 128, 128};
        LinearGradientBrush brush;
        brush.StartPoint({0, 0});
        brush.EndPoint({1, 0});
        shimmer = Storyboard();
        shimmer.RepeatBehavior(RepeatBehaviorHelper::Forever());
        for (int index = 0; index < 3; ++index) {
            GradientStop stop;
            auto shade = color;
            if (index != 1) {
                shade.A = static_cast<uint8_t>(color.A * 0.45);
            }
            stop.Color(shade);
            const double start = -0.5 + index * 0.25;
            stop.Offset(start);
            brush.GradientStops().Append(stop);
            DoubleAnimation sweep;
            sweep.From(start);
            sweep.To(start + 1.5);
            sweep.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(1500)));
            // Gradient stops require dependent animation in WinUI's XAML renderer.
            sweep.EnableDependentAnimation(true);
            Storyboard::SetTarget(sweep, stop);
            Storyboard::SetTargetProperty(sweep, L"Offset");
            shimmer.Children().Append(sweep);
        }
        text.Foreground(brush);
        shimmer.Begin();
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
        const bool waiting = !hasProblem && !finished
            && (phase == Phase::Transcribing || (refining && preview.isEmpty()));
        setShimmer(waiting);
        QString shown = hasProblem ? problem
            : finished                ? status
            : waiting                 ? (phase == Phase::Transcribing
                                             ? QStringLiteral("Transcribing…")
                                             : QStringLiteral("Refining…"))
            : preview.isEmpty()       ? status
                                      : preview;

        // The pill hugs the one line of type like the mac panel: the width
        // follows the measured text between the 420 floor and the screen
        // edge, word by word, while a status line or the delivered message
        // leaves the width where the last preview put it.
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        const int maximumWidth = std::max(
            panelWidth,
            int((monitor.rcWork.right - monitor.rcWork.left) / scale()) - screenEdgeMargin);
        const int maximumCharacters = std::max(20, (maximumWidth - previewChromeWidth) / 7);
        if (!hasProblem && shown.size() > maximumCharacters) {
            shown = QString::fromUtf16(u"\u2026") + shown.right(maximumCharacters - 1);
        }
        const bool sizesToText = hasProblem || (!finished && !waiting && !preview.isEmpty());
        int wantedWidth = width;
        if (sizesToText) {
            wantedWidth = std::clamp(measuredTextWidth(shown) + previewChromeWidth,
                                     panelWidth, maximumWidth);
        }
        const int textWidth = wantedWidth - previewChromeWidth;
        text.Text(hstring(shown.toStdWString()));
        if (finished) {
            text.ClearValue(FrameworkElement::WidthProperty());
            row.HorizontalAlignment(HorizontalAlignment::Center);
        } else {
            text.Width(textWidth);
            row.HorizontalAlignment(HorizontalAlignment::Left);
        }
        level.Visibility(!hasProblem && !finished && phase == Phase::Live
                                 && status.compare(QStringLiteral("listening"), Qt::CaseInsensitive) == 0
                             ? Visibility::Visible
                             : Visibility::Collapsed);
        ring.Visibility(!hasProblem && !finished && refining ? Visibility::Visible
                                                 : Visibility::Collapsed);
        dismiss.Visibility(hasProblem ? Visibility::Visible : Visibility::Collapsed);
        if (wantedWidth != width) {
            resize(wantedWidth);
            if (IsWindowVisible(window)) {
                reposition();
            }
        }
        refreshBanner();
    }

    double scale() const
    {
        return window ? GetDpiForWindow(window) / 96.0 : 1.0;
    }

    int px(int dip) const
    {
        return int(dip * scale() + 0.5);
    }

    // Desired width of the line in the pill's font, the way the mac panel
    // measures its NSString. A detached TextBlock measures fine; if XAML
    // ever hands back nothing, the 7px-per-character estimate stands in.
    int measuredTextWidth(const QString &value)
    {
        probe.Text(hstring(value.toStdWString()));
        constexpr float unbounded = std::numeric_limits<float>::infinity();
        probe.Measure({unbounded, unbounded});
        const int measured = int(std::ceil(probe.DesiredSize().Width));
        return measured > 0 ? measured + 2 : int(value.size()) * 7;
    }

    void resize(int newWidth)
    {
        width = newWidth;
        if (source) {
            source.SiteBridge().MoveAndResize({0, 0, px(width), px(panelHeight)});
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
        const int physicalWidth = px(width);
        const int physicalHeight = px(panelHeight);
        const int x = monitor.rcWork.left
            + (monitor.rcWork.right - monitor.rcWork.left - physicalWidth) / 2;
        const int y = monitor.rcWork.bottom - physicalHeight - px(bottomMargin);
        SetWindowPos(window, HWND_TOPMOST, x, y, physicalWidth, physicalHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (banner && IsWindowVisible(banner)) {
            positionBanner();
        }
    }

    ApplicationController *controller;
    DictationPanel *panel;
    HWND window = nullptr;
    HWND banner = nullptr;
    DesktopWindowXamlSource source{nullptr};
    DesktopWindowXamlSource bannerSource{nullptr};
    Border chrome{nullptr};
    StackPanel bannerRoot{nullptr};
    StackPanel updateRow{nullptr};
    TextBlock updateText{nullptr};
    Button updateAction{nullptr};
    StackPanel whatsNewRow{nullptr};
    TextBlock whatsNewText{nullptr};
    Button whatsNewAction{nullptr};
    QTimer whatsNewAutoHide;
    bool whatsNewHidden = false;
    FontIcon glyph{nullptr};
    TextBlock text{nullptr};
    TextBlock probe{nullptr};
    ProgressBar level{nullptr};
    ProgressRing ring{nullptr};
    Button dismiss{nullptr};
    QString status;
    QString preview;
    QString problem;
    int width = panelWidth;
    quint64 pendingGeneration = 0;
    quint64 presentedGeneration = 0;
    StackPanel row{nullptr};
    Phase phase = Phase::Live;
    Brush normalForeground{nullptr};
    Microsoft::UI::Xaml::Media::Animation::Storyboard shimmer{nullptr};
    bool shimmering = false;
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

void DictationPanel::driveStatusForTest(const QString &status)
{
    m_native->ensureWindow();
    m_native->setStatus(status);
}

void DictationPanel::drivePreviewForTest(const QString &preview)
{
    m_native->ensureWindow();
    m_native->setPreview(preview);
}

void DictationPanel::driveLevelForTest(float level)
{
    m_native->setLevel(level);
}

// Copies the panel's screen rectangle, DWM-composed, so the picture carries
// the acrylic backdrop and rounded corners the user actually sees. The panel
// is topmost, so nothing can sit in front of it.
bool DictationPanel::saveGrabForTest(const QString &path) const
{
    HWND window = m_native->window;
    if (!window || !IsWindowVisible(window)) {
        return false;
    }
    RECT rect{};
    GetWindowRect(window, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top,
           SRCCOPY | CAPTUREBLT);
    QImage image(width, height, QImage::Format_RGB32);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const bool copied = GetDIBits(memory, bitmap, 0, height, image.bits(),
                                  &info, DIB_RGB_COLORS) == height;
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return copied && image.save(path);
}

} // namespace speecher
