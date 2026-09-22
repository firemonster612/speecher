#include "frontend/win/DictationPanel.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "frontend/win/SettingsPage.h"
#include "ui/WaveformModel.h"

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
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#pragma pop_macro("GetCurrentTime")

#include <QImage>
#include <QElapsedTimer>
#include <QTimer>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;

// The panel's layout constants are DIPs, as the XAML content measures them;
// every HWND move and resize scales them by the window's DPI.
constexpr int panelWidth = 126;
constexpr int panelHeight = 48;
constexpr int previewChromeWidth = 48;
constexpr int compactStripHeight = 28;
constexpr int previewTopPadding = 12;
constexpr int previewStripSpacing = 8;
constexpr int previewBottomPadding = 8;
// The shoulder sits as far below the text as the pill's top sits above it,
// so the wide bar reads evenly padded around the preview line.
constexpr int previewShoulderDrop = previewTopPadding;
constexpr int maximumPreviewWidth = 488;
constexpr int screenEdgeMargin = 80;
constexpr int bottomMargin = 28;
constexpr int bannerGap = 12;
constexpr auto windowClassName = L"SpeecherDictationPanel";

// Same dot geometry as the Linux waveform; the travelling crest and level
// mapping come from the shared model in ui/WaveformModel.h.
constexpr int levelBarCount = waveform::barCount;
constexpr float barDotHeight = 3.2f;

// Whether every pixel is the same colour, which is what a capture with no
// desktop behind it looks like.
bool isUniform(const QImage &image)
{
    if (image.isNull()) {
        return true;
    }
    const QRgb first = image.pixel(0, 0);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != first) {
                return false;
            }
        }
    }
    return true;
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

win::UpdateChipState win::updateChipState(UpdateController::State state, const QString &version,
                                          int percent, const QString &error, bool repeatedFailure,
                                          bool manualInstall, DictationState sessionState)
{
    const bool canAct = sessionState == DictationState::Idle
        || sessionState == DictationState::Error;
    using State = UpdateController::State;
    switch (state) {
    case State::UpdateAvailable:
        // The caller hands the display form: a stable version number, or a
        // nightly's build and commit, which is what changes between nightlies.
        return {QStringLiteral("Speecher %1 available").arg(version),
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
        barTimer.setInterval(waveform::frameIntervalMs);
        barClock.start();
        connect(&barTimer, &QTimer::timeout, this, &Native::animateBars);
        // The same five seconds the Qt popup counts down; the Dismiss button
        // remains the early way out.
        problemAutoDismiss.setSingleShot(true);
        problemAutoDismiss.setInterval(5000);
        connect(&problemAutoDismiss, &QTimer::timeout, this, &Native::dismissProblem);
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
                    if (bars) { bars.Opacity(frozen ? 0.4 : 1.0); }
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
            phase = Phase::Live;
            status = QStringLiteral("Renewing sign-in…");
            preview.clear();
            refresh();
        });
        connect(session, &DictationSession::popupListeningIndicatorRequested, this, [this] {
            phase = Phase::Live;
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
                    native->resize(native->width, native->height);
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
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED,
            windowClassName, L"Speecher dictation", WS_POPUP,
            0, 0, panelWidth, panelHeight, nullptr, nullptr,
            windowClass.hInstance, this);
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);

        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));

        source = DesktopWindowXamlSource();
        source.Initialize(Microsoft::UI::GetWindowIdFromWindow(window));

        chrome = Microsoft::UI::Xaml::Markup::XamlReader::Load(
            LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" />)")
            .as<Border>();
        chrome.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        outline = Microsoft::UI::Xaml::Markup::XamlReader::Load(
            LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Fill="{ThemeResource AcrylicBackgroundFillColorDefaultBrush}" Stroke="{ThemeResource ControlStrongStrokeColorDefaultBrush}" StrokeThickness="1"/>)")
            .as<Microsoft::UI::Xaml::Shapes::Path>();
        outline.Margin({0.5, 0.5, 0.5, 0.5});
        outline.HorizontalAlignment(HorizontalAlignment::Left);
        outline.VerticalAlignment(VerticalAlignment::Top);
        content = StackPanel();
        content.VerticalAlignment(VerticalAlignment::Center);
        row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(10);
        row.VerticalAlignment(VerticalAlignment::Center);

        glyph = FontIcon();
        glyph.FontSize(20);
        row.Children().Append(glyph);

        text = TextBlock();
        text.VerticalAlignment(VerticalAlignment::Center);
        text.TextTrimming(TextTrimming::CharacterEllipsis);
        text.MaxLines(1);
        text.Width(panelWidth);
        probe = TextBlock();

        bars = StackPanel();
        bars.Orientation(Orientation::Horizontal);
        bars.Spacing(3.2);
        bars.Width(92.8);
        bars.Height(panelHeight);
        bars.VerticalAlignment(VerticalAlignment::Center);
        bars.HorizontalAlignment(HorizontalAlignment::Center);
        for (int i = 0; i < levelBarCount; ++i) {
            Microsoft::UI::Xaml::Shapes::Rectangle bar;
            bar.Width(3.2);
            bar.RadiusX(0.8);
            bar.RadiusY(0.8);
            bar.Height(barDotHeight);
            bar.VerticalAlignment(VerticalAlignment::Center);
            bar.Fill(text.Foreground());
            bars.Children().Append(bar);
            barRects.push_back(bar);
        }
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(bars, L"Input level");
        waveform = Border();
        waveform.Width(panelWidth);
        waveform.Child(bars);
        row.Children().Append(waveform);
        row.Children().Append(text);

        previewText = TextBlock();
        previewText.VerticalAlignment(VerticalAlignment::Center);
        previewText.TextAlignment(TextAlignment::Center);
        previewText.MaxLines(1);
        previewText.TextTrimming(TextTrimming::CharacterEllipsis);
        previewText.Margin({24, previewTopPadding, 24, 0});
        content.Children().Append(previewText);

        dismiss = Button();
        dismiss.Content(box_value(L"Dismiss"));
        dismiss.Visibility(Visibility::Collapsed);
        dismiss.Click([this](const auto &, const auto &) {
            dismissProblem();
        });
        row.Children().Append(dismiss);

        content.Children().Append(row);
        Grid layers;
        layers.Children().Append(outline);
        layers.Children().Append(content);
        chrome.Child(layers);
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
        chrome.HorizontalAlignment(HorizontalAlignment::Center);
        chrome.VerticalAlignment(VerticalAlignment::Bottom);
        Grid surface;
        surface.Children().Append(chrome);
        source.Content(surface);
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
            updates->state(), updates->availableVersionDisplay(), updates->downloadPercent(),
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
        const int y = pill.bottom - px(height) - bannerHeight - px(bannerGap);
        SetWindowPos(banner, HWND_TOPMOST, x, y, bannerWidth, bannerHeight,
                     SWP_NOACTIVATE);
        bannerSource.SiteBridge().MoveAndResize({0, 0, bannerWidth, bannerHeight});
    }

    void show(quint64 generation)
    {
        // A dictation starting inside a problem's five seconds must not be
        // torn down when that problem's timer fires.
        problemAutoDismiss.stop();
        problem.clear();
        // The previous dictation's words are spent; the session's clearing
        // preview can be dropped by the frozen guard, so clear here too.
        preview.clear();
        completed = false;
        phase = Phase::Live;
        pendingGeneration = generation;
        level.restart(barClock.elapsed());
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
        problemAutoDismiss.start();
    }

    void hide()
    {
        whatsNewAutoHide.stop();
        problemAutoDismiss.stop();
        barTimer.stop();
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
            // The rectangles captured the text brush at creation; a theme
            // change hands them the newly resolved one. While the status text
            // shimmers its foreground is the animated gradient, which would
            // freeze into the bars as a half-swept smear.
            const auto ink = shimmering ? normalForeground : text.Foreground();
            for (auto &bar : barRects) {
                bar.Fill(ink);
            }
        }
    }

    void dismissProblem()
    {
        problem.clear();
        hide();
        // Only an errored session is the one this problem belongs to. On a
        // live session stopListening() cancels the refinement or stops the
        // mic, which the countdown must never do behind the user's back; the
        // Qt front end has always guarded it this way.
        if (controller->session()->state() == DictationState::Error) {
            controller->stopListening();
        }
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
        level.addChunk(value);
    }

    void animateBars()
    {
        const qint64 now = barClock.elapsed();
        const float elapsed = std::clamp((now - lastFrame) / 1000.0f, 0.0f, 0.1f);
        lastFrame = now;
        if (frozen) {
            return;
        }
        barPhase = std::fmod(barPhase + elapsed, 1.0f);
        level.advance(now);
        for (int i = 0; i < int(barRects.size()); ++i) {
            const float phase = barPhase - float(i) / levelBarCount;
            const float height = float(barDotHeight * level.audioScale() * waveform::bulge(i)
                                       * waveform::waveMultiplier(phase - std::floor(phase)));
            barRects[i].Height(height);
            barRects[i].RadiusY(height / 4);
        }
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
        // Only errors have controls. The transparent space around a live
        // capsule must not intercept clicks in the target application.
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        const LONG_PTR wantedStyle = hasProblem ? style & ~WS_EX_TRANSPARENT : style | WS_EX_TRANSPARENT;
        if (style != wantedStyle) {
            SetWindowLongPtrW(window, GWL_EXSTYLE, wantedStyle);
        }
        // A finished delivery: the outcome message is the whole story, so the
        // spent preview words go and the icon and message centre in the pill.
        const bool finished = completed && !hasProblem;
        glyph.Glyph(hstring((refining && !hasProblem
                                 ? QString::fromUtf16(u"\uE8A9")
                                 : phaseGlyph(status, hasProblem))
                                .toStdWString()));
        const bool renewing = status == QStringLiteral("Renewing sign-in…");
        const bool waiting = !hasProblem && !finished && (phase != Phase::Live || renewing);
        const bool listening = !hasProblem && !finished && !waiting;
        const bool showPreview = !hasProblem && !finished && !preview.isEmpty();
        setShimmer(waiting);
        QString shown = hasProblem ? problem : finished ? status
            : renewing ? status : phase == Phase::Transcribing ? QStringLiteral("Transcribing…")
            : waiting ? QStringLiteral("Refining…") : QString();
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        const int minimumWidth = hasProblem ? 420 : panelWidth;
        const int maximumWidth = std::max(minimumWidth,
            int((monitor.rcWork.right - monitor.rcWork.left) / scale()) - screenEdgeMargin);
        int wantedWidth = hasProblem || finished
            ? std::clamp(measuredTextWidth(shown) + (hasProblem ? 150 : 68),
                         minimumWidth, maximumWidth)
            : waiting ? std::max(panelWidth, measuredTextWidth(shown) + 32) : panelWidth;
        if (showPreview) {
            const int transcriptMaximum = std::min(maximumPreviewWidth, maximumWidth);
            const QString visible = fitPreview(preview, transcriptMaximum - previewChromeWidth);
            // Match macOS: hug the transcript until it reaches the width cap.
            // Measuring the elided tail would make the width twitch at overflow.
            wantedWidth = std::min(transcriptMaximum,
                std::max(panelWidth + previewChromeWidth,
                         measuredTextWidth(preview) + previewChromeWidth));
            previewText.Text(hstring(visible.toStdWString()));
            previewText.Width(wantedWidth - previewChromeWidth);
        }
        previewText.Visibility(showPreview ? Visibility::Visible : Visibility::Collapsed);
        row.Padding(hasProblem || finished ? Thickness{12, 0, 12, 0} : Thickness{});
        row.Margin({0, showPreview ? double(previewStripSpacing) : 0.0, 0, 0});
        content.Padding({0, 0, 0, showPreview ? double(previewBottomPadding) : 0.0});
        text.Text(hstring(shown.toStdWString()));
        text.Visibility(listening ? Visibility::Collapsed : Visibility::Visible);
        text.Width(hasProblem ? wantedWidth - 150 : finished ? wantedWidth - 68 : wantedWidth);
        text.TextAlignment(TextAlignment::Center);
        row.HorizontalAlignment(HorizontalAlignment::Center);
        glyph.Visibility(hasProblem || finished ? Visibility::Visible : Visibility::Collapsed);
        waveform.Visibility(listening ? Visibility::Visible : Visibility::Collapsed);
        bars.Opacity(frozen ? 0.4 : 1.0);
        if (listening && !barTimer.isActive()) {
            lastFrame = barClock.elapsed();
            barTimer.start();
        } else if (!listening) {
            barTimer.stop();
        }
        dismiss.Visibility(hasProblem ? Visibility::Visible : Visibility::Collapsed);
        // Measure the native font so both the contour and strip clear its ink.
        probe.Text(L"Ag");
        probe.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
        const int lineHeight = int(std::ceil(probe.DesiredSize().Height));
        const int stripHeight = showPreview ? waiting ? lineHeight + 6 : compactStripHeight : panelHeight;
        row.Height(stripHeight);
        bars.Height(stripHeight);
        const int wantedHeight = showPreview
            ? previewTopPadding + lineHeight + previewStripSpacing + stripHeight + previewBottomPadding
            : panelHeight;
        // Keep the native host stable while XAML resizes the visible capsule.
        // Resizing the HWND first can clip the previous composition frame.
        surfaceWidth = hasProblem ? wantedWidth : std::max(wantedWidth, std::min(maximumPreviewWidth, maximumWidth));
        surfaceHeight = hasProblem ? wantedHeight
            : previewTopPadding + lineHeight + previewStripSpacing
                + std::max(compactStripHeight, lineHeight + 6) + previewBottomPadding;
        resize(wantedWidth, wantedHeight);
        updateOutline(showPreview ? previewTopPadding + lineHeight + previewShoulderDrop : 0,
                      waiting ? measuredTextWidth(shown) : 92.8);
        chrome.UpdateLayout();
        if (IsWindowVisible(window)) {
            reposition();
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

    QString fitPreview(const QString &value, int maximumWidth)
    {
        if (measuredTextWidth(value) <= maximumWidth) {
            return value;
        }
        std::vector<qsizetype> boundaries;
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, value);
        for (qsizetype position = finder.toNextBoundary(); position >= 0;
             position = finder.toNextBoundary()) {
            boundaries.push_back(position);
        }
        const QString ellipsis = QString::fromUtf16(u"\u2026");
        size_t first = 0;
        size_t last = boundaries.size() - 1;
        while (first < last) {
            const size_t middle = first + (last - first) / 2;
            if (measuredTextWidth(ellipsis + value.mid(boundaries[middle])) <= maximumWidth) {
                last = middle;
            } else {
                first = middle + 1;
            }
        }
        return ellipsis + value.mid(boundaries[first]);
    }

    // Same circular end caps and concave joins as the accepted Qt preview.
    void updateOutline(double shoulder, double inkWidth)
    {
        // Keep the whole stroke inside the HWND rather than clipping its edges.
        const double width = this->width - 1;
        const double height = this->height - 1;
        const double cap = shoulder / 2;
        const double half = inkWidth / 2 + 10;
        const double left = width / 2.0 - half;
        const double right = width / 2.0 + half;
        const double lobeHeight = height - shoulder;
        double fillet = std::min(12.0, left - cap);
        double radius = std::min(24.0, half);
        if (fillet + radius > lobeHeight) {
            const double scale = lobeHeight / (fillet + radius);
            fillet *= scale;
            radius *= scale;
        }
        PathFigure figure;
        figure.IsClosed(true);
        const auto line = [&](double x, double y) {
            LineSegment segment;
            segment.Point({float(x), float(y)});
            figure.Segments().Append(segment);
        };
        const auto arc = [&](double x, double y, double r, SweepDirection direction) {
            ArcSegment segment;
            segment.Point({float(x), float(y)});
            segment.Size({float(r), float(r)});
            segment.SweepDirection(direction);
            figure.Segments().Append(segment);
        };
        if (shoulder <= 0 || lobeHeight <= 0 || fillet < 4) {
            radius = std::min(24.0, height / 2.0);
            figure.StartPoint({float(radius), 0});
            line(width - radius, 0);
            arc(width, radius, radius, SweepDirection::Clockwise);
            line(width, height - radius);
            arc(width - radius, height, radius, SweepDirection::Clockwise);
            line(radius, height);
            arc(0, height - radius, radius, SweepDirection::Clockwise);
            line(0, radius);
            arc(radius, 0, radius, SweepDirection::Clockwise);
        } else {
            figure.StartPoint({float(cap), 0});
            line(width - cap, 0);
            arc(width - cap, shoulder, cap, SweepDirection::Clockwise);
            line(right + fillet, shoulder);
            arc(right, shoulder + fillet, fillet, SweepDirection::Counterclockwise);
            line(right, height - radius);
            arc(right - radius, height, radius, SweepDirection::Clockwise);
            line(left + radius, height);
            arc(left, height - radius, radius, SweepDirection::Clockwise);
            line(left, shoulder + fillet);
            arc(left - fillet, shoulder, fillet, SweepDirection::Counterclockwise);
            line(cap, shoulder);
            arc(cap, 0, cap, SweepDirection::Clockwise);
        }
        PathGeometry geometry;
        geometry.Figures().Append(figure);
        outline.Data(geometry);
    }

    void resize(int newWidth, int newHeight = panelHeight)
    {
        width = newWidth;
        height = newHeight;
        chrome.Width(width);
        chrome.Height(height);
        if (source) {
            source.SiteBridge().MoveAndResize({0, 0, px(surfaceWidth), px(surfaceHeight)});
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
        const int physicalWidth = px(surfaceWidth);
        const int physicalHeight = px(surfaceHeight);
        const int x = monitor.rcWork.left
            + (monitor.rcWork.right - monitor.rcWork.left - physicalWidth) / 2;
        const int y = monitor.rcWork.bottom - physicalHeight - px(bottomMargin);
        RECT current{};
        GetWindowRect(window, &current);
        if (!IsWindowVisible(window) || current.left != x || current.top != y
            || current.right - current.left != physicalWidth
            || current.bottom - current.top != physicalHeight) {
            SetWindowPos(window, HWND_TOPMOST, x, y, physicalWidth, physicalHeight,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        if (banner && IsWindowVisible(banner)) {
            positionBanner();
        }
    }

    QRect controlGeometry(const FrameworkElement &control) const
    {
        if (!IsWindowVisible(window) || control.Visibility() == Visibility::Collapsed) {
            return {};
        }
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const auto point = control.TransformToVisual(nullptr).TransformPoint({0, 0});
        return QRect(bounds.left + qRound(point.X * scale()),
                     bounds.top + qRound(point.Y * scale()),
                     qRound(control.ActualWidth() * scale()),
                     qRound(control.ActualHeight() * scale()));
    }

    ApplicationController *controller;
    DictationPanel *panel;
    HWND window = nullptr;
    TextBlock previewText{nullptr};
    Border waveform{nullptr};
    HWND banner = nullptr;
    DesktopWindowXamlSource source{nullptr};
    DesktopWindowXamlSource bannerSource{nullptr};
    Border chrome{nullptr};
    Microsoft::UI::Xaml::Shapes::Path outline{nullptr};
    StackPanel content{nullptr};
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
    StackPanel bars{nullptr};
    std::vector<Microsoft::UI::Xaml::Shapes::Rectangle> barRects;
    QTimer barTimer;
    QElapsedTimer barClock;
    qint64 lastFrame = 0;
    waveform::LevelModel level;
    float barPhase = 0.0f;
    QTimer problemAutoDismiss;
    Button dismiss{nullptr};
    QString status;
    QString preview;
    QString problem;
    int width = panelWidth;
    int height = panelHeight;
    int surfaceWidth = maximumPreviewWidth;
    int surfaceHeight = panelHeight;
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

int DictationPanel::levelBarCountForTest() const
{
    return int(m_native->barRects.size());
}

QRect DictationPanel::capsuleGeometryForTest() const
{
    return m_native->controlGeometry(m_native->chrome);
}

QRect DictationPanel::waveformGeometryForTest() const
{
    return m_native->controlGeometry(m_native->waveform);
}

QRect DictationPanel::previewGeometryForTest() const
{
    return m_native->controlGeometry(m_native->previewText);
}

QString DictationPanel::previewTextForTest() const
{
    return QString::fromStdWString(std::wstring(m_native->previewText.Text()));
}

bool DictationPanel::previewTextFitsForTest() const
{
    return m_native->measuredTextWidth(previewTextForTest()) <= m_native->chrome.ActualWidth() - previewChromeWidth;
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
    const QRect capsule = capsuleGeometryForTest();
    RECT rect{capsule.left(), capsule.top(), capsule.x() + capsule.width(), capsule.y() + capsule.height()};
    if (qEnvironmentVariableIsSet("SPEECHER_TEST_PANEL_BANNERS")
        && m_native->banner && IsWindowVisible(m_native->banner)) {
        RECT notices{};
        GetWindowRect(m_native->banner, &notices);
        UnionRect(&rect, &rect, &notices);
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    HDC screen = GetDC(nullptr);
    if (!screen) {
        return false;
    }
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    if (!memory || !bitmap) {
        if (memory) {
            DeleteDC(memory);
        }
        ReleaseDC(nullptr, screen);
        return false;
    }
    HGDIOBJ previous = SelectObject(memory, bitmap);
    // A failed blit leaves the bitmap filled with uninitialised GDI memory,
    // which would still save as a perfectly valid-looking PNG.
    const bool blitted = BitBlt(memory, 0, 0, width, height, screen,
                                rect.left, rect.top, SRCCOPY | CAPTUREBLT);
    // GetDIBits requires the bitmap not be selected into any device context.
    SelectObject(memory, previous);
    QImage image(width, height, QImage::Format_RGB32);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const bool copied = blitted
        && GetDIBits(memory, bitmap, 0, height, image.bits(),
                     &info, DIB_RGB_COLORS) == height;
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied || isUniform(image)) {
        // A session without a composited desktop blits solid black, which
        // saves as a perfectly valid PNG and would be uploaded as evidence.
        return false;
    }
    return image.save(path);
}

} // namespace speecher
