#include "frontend/win/DictationPanel.h"

#include "app/ApplicationController.h"
#include "app/UpdateBanner.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/DictationTypes.h"
#include "dictation/PopupGeometry.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/win/SettingsPage.h"
#include "frontend/win/WaveformBars.h"

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
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Text.h>
#pragma pop_macro("GetCurrentTime")

#include <QImage>
#include <QElapsedTimer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Hosting;
using namespace Microsoft::UI::Xaml::Media;

// The panel's layout constants are DIPs, as the XAML content measures them;
// every HWND move and resize scales them by the window's DPI. The capsule's
// own measurements are the ones every platform shares (PopupGeometry.h).
constexpr int panelWidth = popup::kPillMinimumWidth;
// The error capsule keeps its own floor, as on the other platforms.
constexpr int problemMinimumWidth = 126;
constexpr int panelHeight = popup::kPillHeight;
constexpr int previewChromeWidth = 2 * popup::kPreviewSideMargin;
constexpr int compactStripHeight = popup::kCompactStripHeight;
constexpr int previewTopPadding = popup::kPreviewTopMargin;
constexpr int previewStripSpacing = popup::kPreviewStripSpacing;
constexpr int previewBottomPadding = popup::kPreviewBottomMargin;
constexpr int previewShoulderDrop = popup::kShoulderDrop;
constexpr int maximumPreviewWidth = popup::kMaxPreviewWidth + previewChromeWidth;
// WinUI's body text, and the smaller size of the preview line and the status
// beside the buttons.
constexpr double bodyFontSize = 14;
constexpr double previewFontSize = bodyFontSize * popup::kPreviewFontScale;
// An error keeps its own, roomier capsule: at least this tall, its message
// padded this much above and below, and its parts this far apart. Receipts
// share the gap.
constexpr int problemMinimumHeight = 48;
constexpr int problemPadding = 12;
constexpr int messageRowSpacing = 10;
constexpr int screenEdgeMargin = 80;
constexpr int bottomMargin = 28;
constexpr int bannerGap = 12;
// A problem's padding, warning glyph, Dismiss button and the gaps between.
constexpr int problemChromeWidth = 150;
// A receipt's padding and glyph beside its message.
constexpr int receiptChromeWidth = 68;
// A button's padding and border around its caption, and the gap before it.
constexpr int buttonChromeWidth = 36;
// The countdown bar under a problem and its margin.
constexpr int problemBarHeight = 16;
// The pause and cancel buttons either side of the waveform, round like the
// notices' close button, and the gap beside each.
constexpr int sessionButtonSize = popup::kButtonSize;
constexpr int sessionButtonGap = popup::kButtonGap;
// The spinner in pause's slot, a little inside the slot as the icons are.
constexpr int spinnerSize = 16;
constexpr win::WaveformGeometry panelBars{popup::kBarCount, popup::kBarWidth, popup::kBarGap,
                                          popup::kBarDotHeight};
// A selection edit's review: a card as wide as an error's wrapped text plus
// the preview's side margins, its changed words marked with a fifth of a
// tone over the card, as the Linux and macOS badges fill.
constexpr int reviewCardWidth = kPopupErrorWrapWidth + previewChromeWidth;
constexpr double editTintShare = 0.2;
constexpr auto windowClassName = L"SpeecherDictationPanel";

// A brush's colour, for brushes built from the theme's text. High-contrast
// themes can hand out a non-solid foreground brush.
winrt::Windows::UI::Color solidColor(const Brush &brush)
{
    const auto solid = brush.try_as<SolidColorBrush>();
    return solid ? solid.Color() : winrt::Windows::UI::Color{255, 128, 128, 128};
}

// WinUI's accent button, which marks the default action.
Microsoft::UI::Xaml::Style accentButtonStyle()
{
    return Application::Current().Resources()
        .Lookup(box_value(hstring(L"AccentButtonStyle")))
        .as<Microsoft::UI::Xaml::Style>();
}

// A text highlighter's brush: a fifth of the tone, over the card behind it.
// Highlighters take only solid brushes.
SolidColorBrush editTint(const Brush &tone)
{
    auto color = solidColor(tone);
    color.A = static_cast<uint8_t>(color.A * editTintShare);
    return SolidColorBrush(color);
}

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

// Segoe Fluent Icons for each receipt: sent into the Target, left on the
// clipboard, delivered raw because refinement failed, or cancelled.
QString outcomeGlyph(PopupOutcome outcome)
{
    switch (outcome) {
    case PopupOutcome::Inserted: return QString::fromUtf16(u"\uE724");
    case PopupOutcome::Copied: return QString::fromUtf16(u"\uF0E3");
    case PopupOutcome::Fallback: return QString::fromUtf16(u"\uE946");
    case PopupOutcome::Error: return QString::fromUtf16(u"\uE7BA");
    case PopupOutcome::Cancelled: return QString::fromUtf16(u"\uE711");
    }
    return QString::fromUtf16(u"\uE946");
}

// Whether Windows has somewhere to send a fix to: a settings page, or the
// microphone's privacy page. The other panels belong to other systems.
bool fixAvailable(const PopupErrorAction &fix)
{
    return fix.fix == ErrorFix::SettingsPage || fix.fix == ErrorFix::MicrophonePermission;
}

QString phaseGlyph(const QString &status, bool problem)
{
    if (problem) {
        return outcomeGlyph(PopupOutcome::Error);
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
    enum class Phase { Live, Transcribing, Refining };

    Native(ApplicationController *owner, DictationPanel *q)
        : QObject(q)
        , controller(owner)
        , panel(q)
    {
        // The countdown every platform shows; Dismiss stays the early way out.
        problemAutoDismiss.setSingleShot(true);
        connect(&problemAutoDismiss, &QTimer::timeout, this, &Native::dismissProblem);
        whatsNewAutoHide.setSingleShot(true);
        whatsNewAutoHide.setInterval(6000);
        connect(&whatsNewAutoHide, &QTimer::timeout, this, [this] {
            whatsNewHidden = true;
            refresh();
        });
        connect(controller->updateBanner(), &UpdateBanner::changed, this, &Native::refreshBanner);
        connect(controller, &ApplicationController::whatsNewChanged, this, [this] {
            whatsNewHidden = false;
            if (window && IsWindowVisible(window)) {
                whatsNewAutoHide.start();
            }
            refresh();
        });
        DictationSession *session = controller->session();
        connect(session, &DictationSession::stateChanged, this, [this](const QString &name) {
            sessionState = name;
            refresh();
        });
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
                    if (wave) { wave->setFrozen(frozen); }
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
            status = renewingSignInText();
            preview.clear();
            refresh();
        });
        connect(session, &DictationSession::popupListeningIndicatorRequested, this, [this] {
            phase = Phase::Live;
            setStatus(QStringLiteral("Listening"));
        });
        connect(session, &DictationSession::popupMessageRequested, this,
                [this](const QString &message, PopupOutcome value, const PopupErrorAction &action) {
                    status = message;
                    outcome = value;
                    fix = fixAvailable(action) ? action : PopupErrorAction{};
                    completed = true;
                    refresh();
                });
        connect(session, &DictationSession::popupErrorRequested, this, &Native::showProblem);
        connect(session, &DictationSession::popupSelectionEditReviewRequested, this, &Native::showReview);
        // A follow-up runs through the states of a dictation while the card
        // stays up, so the card goes on this signal, not on a state.
        connect(session, &DictationSession::popupSelectionEditReviewEnded, this, [this] {
            reviewShown = false;
            refresh();
        });
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
        // Clicking the pause or cancel button must leave the Target focused.
        if (message == WM_MOUSEACTIVATE) {
            return MA_NOACTIVATE;
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
        outline = capsuleShape();
        content = StackPanel();
        content.VerticalAlignment(VerticalAlignment::Center);
        row = StackPanel();
        row.Orientation(Orientation::Horizontal);
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

        wave = new win::WaveformBars(panelBars, this);
        wave->element().Height(panelHeight);
        wave->setInk(text.Foreground());
        waveform = Border();
        waveform.Width(panelWidth);
        waveform.Child(wave->element());
        const auto sessionButton = [](const wchar_t *glyphText) {
            Button button;
            button.Width(sessionButtonSize);
            button.Height(sessionButtonSize);
            button.Padding({0, 0, 0, 0});
            button.CornerRadius({sessionButtonSize / 2.0, sessionButtonSize / 2.0,
                                 sessionButtonSize / 2.0, sessionButtonSize / 2.0});
            button.VerticalAlignment(VerticalAlignment::Center);
            // Never focused: the Target keeps the keyboard.
            button.IsTabStop(false);
            button.AllowFocusOnInteraction(false);
            FontIcon icon;
            icon.Glyph(glyphText);
            icon.FontSize(popup::kButtonIconSize);
            button.Content(icon);
            button.Visibility(Visibility::Collapsed);
            return button;
        };
        pauseButton = sessionButton(L"\uE769");
        pauseButton.Click([this](const auto &, const auto &) {
            controller->session()->togglePause();
        });
        cancelButton = sessionButton(L"\uE711");
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(cancelButton, win::hs(cancelCaption()));
        ToolTipService::SetToolTip(cancelButton, box_value(win::hs(cancelCaption())));
        cancelButton.Click([this](const auto &, const auto &) { controller->cancel(); });
        // While transcribing and refining a spinner takes pause's slot, so the
        // status (Transcribing…) sits between two circles as the bars do.
        ProgressRing spinner;
        spinner.IsIndeterminate(true);
        spinner.Width(spinnerSize);
        spinner.Height(spinnerSize);
        spinner.HorizontalAlignment(HorizontalAlignment::Center);
        spinner.VerticalAlignment(VerticalAlignment::Center);
        busySlot = Border();
        busySlot.Width(sessionButtonSize);
        busySlot.Height(sessionButtonSize);
        busySlot.VerticalAlignment(VerticalAlignment::Center);
        busySlot.Child(spinner);
        busySlot.Visibility(Visibility::Collapsed);
        row.Children().Append(pauseButton);
        row.Children().Append(busySlot);
        row.Children().Append(waveform);
        row.Children().Append(text);
        row.Children().Append(cancelButton);

        previewText = TextBlock();
        previewText.HorizontalAlignment(HorizontalAlignment::Center);
        previewText.VerticalAlignment(VerticalAlignment::Center);
        previewText.TextAlignment(TextAlignment::Center);
        previewText.MaxLines(1);
        previewText.TextTrimming(TextTrimming::CharacterEllipsis);
        previewText.FontSize(previewFontSize);
        previewText.Margin({popup::kPreviewSideMargin, previewTopPadding, popup::kPreviewSideMargin, 0});
        content.Children().Append(previewText);

        // The one fix of an error or an outcome, ahead of Dismiss as the way
        // forward.
        fixButton = Button();
        fixButton.Style(accentButtonStyle());
        fixButton.Visibility(Visibility::Collapsed);
        fixButton.Click([this](const auto &, const auto &) {
            const PopupErrorAction chosen = fix;
            dismissProblem();
            emit panel->fixRequested(chosen);
        });
        row.Children().Append(fixButton);
        dismiss = Button();
        dismiss.Content(box_value(win::hs(popupDismissCaption())));
        dismiss.Visibility(Visibility::Collapsed);
        dismiss.Click([this](const auto &, const auto &) {
            dismissProblem();
        });
        row.Children().Append(dismiss);

        content.Children().Append(row);
        // The problem's countdown, draining over the time it has left.
        countdown = ProgressBar();
        countdown.Minimum(0);
        countdown.Margin({24, 0, 24, 12});
        countdown.Visibility(Visibility::Collapsed);
        content.Children().Append(countdown);
        buildReviewCard();
        countdownTick.setInterval(50);
        // An error holds its countdown while the pointer is on it, so a long
        // one can be read to the end. The tick reads the cursor rather than
        // waiting for pointer events, which never come when the error appears
        // under a pointer that is not moving.
        connect(&countdownTick, &QTimer::timeout, this, [this] {
            if (pointerOverChrome()) {
                if (problemAutoDismiss.isActive()) {
                    pausedRemainingMs = std::max(problemAutoDismiss.remainingTime(), 1);
                    problemAutoDismiss.stop();
                }
                return;
            }
            if (pausedRemainingMs > 0) {
                problemAutoDismiss.start(std::exchange(pausedRemainingMs, 0));
            }
            countdown.Value(problemAutoDismiss.remainingTime());
        });
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
        // A screen reader hears an error as it arrives.
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetLiveSetting(
            text, Microsoft::UI::Xaml::Automation::Peers::AutomationLiveSetting::Assertive);
        chrome.HorizontalAlignment(HorizontalAlignment::Center);
        // The review card, when there is one, sits above the capsule; the
        // two stand on the window's bottom edge.
        panelStack = StackPanel();
        panelStack.HorizontalAlignment(HorizontalAlignment::Center);
        panelStack.VerticalAlignment(VerticalAlignment::Bottom);
        panelStack.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        panelStack.Children().Append(reviewFrame);
        panelStack.Children().Append(chrome);
        Grid surface;
        surface.Children().Append(panelStack);
        // The island takes a new size from the bridge's MoveAndResize only
        // when its StateChanged event arrives, after refresh() has laid the
        // capsule out in the old size and set the click region from that.
        // The region follows the capsule to where the new size puts it.
        surface.SizeChanged([this](const auto &, const auto &) { limitClicksToCapsule(); });
        source.Content(surface);
        resize(panelWidth);
    }

    // A selection edit's review, in the capsule's place while the session
    // waits in Reviewing: what was said, the edit, and its summary beside
    // Keep original and Replace. While a follow-up is dictated it stays above
    // the capsule, its edit dimmed and its footer gone. A card of its own, in
    // the plain rounded outline an error's capsule takes.
    void buildReviewCard()
    {
        reviewCard = StackPanel();
        reviewCard.Spacing(popup::kReviewSpacing);
        reviewCard.Padding({popup::kPreviewSideMargin, popup::kReviewVerticalMargin,
                            popup::kPreviewSideMargin, popup::kReviewVerticalMargin});
        // layOutReviewCard() sizes the window from a Measure taken as the
        // review changes, before WinUI's own layout pass. When the edit
        // shrinks back under the scroll viewer's line limit, that pass can
        // settle the card at another height, and the window keeps the first
        // one until some later refresh. The window follows the card's settled
        // height. The card keeps its own height in its frame rather than
        // stretching to the outline, which is still the measured size then;
        // stretched, it would never report the settled one.
        reviewCard.VerticalAlignment(VerticalAlignment::Top);
        reviewCard.SizeChanged([this](const auto &, const SizeChangedEventArgs &args) {
            if (reviewShown && int(std::ceil(args.NewSize().Height)) != reviewHeight) {
                reviewLaidOutWidth = 0;
                refresh();
            }
        });

        reviewInstruction = TextBlock();
        reviewInstruction.FontSize(previewFontSize);
        reviewInstruction.TextWrapping(TextWrapping::Wrap);
        reviewCard.Children().Append(reviewInstruction);

        reviewEdit = TextBlock();
        reviewEdit.FontSize(previewFontSize);
        reviewEdit.TextWrapping(TextWrapping::Wrap);
        reviewScroll = ScrollViewer();
        reviewScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        reviewScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        reviewScroll.IsTabStop(false);
        reviewScroll.Content(reviewEdit);
        reviewCard.Children().Append(reviewScroll);

        reviewSummary = TextBlock();
        reviewSummary.FontSize(previewFontSize);
        reviewSummary.VerticalAlignment(VerticalAlignment::Center);
        reviewToggle = HyperlinkButton();
        reviewToggle.FontSize(previewFontSize);
        reviewToggle.Padding({0, 0, 0, 0});
        reviewToggle.VerticalAlignment(VerticalAlignment::Center);
        reviewToggle.IsTabStop(false);
        reviewToggle.AllowFocusOnInteraction(false);
        reviewToggle.Click([this](const auto &, const auto &) {
            reviewWhole = !reviewWhole;
            fillReview();
            reviewScroll.ChangeView(nullptr, 0.0, nullptr, true);
            refresh();
        });
        StackPanel summaryRow;
        summaryRow.Orientation(Orientation::Horizontal);
        summaryRow.Spacing(popup::kReviewSpacing);
        summaryRow.Children().Append(reviewSummary);
        summaryRow.Children().Append(reviewToggle);
        // How to ask for more changes, under the summary.
        reviewHint = TextBlock();
        reviewHint.FontSize(previewFontSize);
        reviewHint.TextWrapping(TextWrapping::Wrap);
        StackPanel summary;
        summary.VerticalAlignment(VerticalAlignment::Center);
        summary.Children().Append(summaryRow);
        summary.Children().Append(reviewHint);

        reviewKeep = keyedButton(keepOriginalCaption());
        reviewKeep.Click([this](const auto &, const auto &) { emit panel->keepOriginalRequested(); });
        reviewReplace = keyedButton(replaceSelectionCaption());
        reviewReplace.Click([this](const auto &, const auto &) { emit panel->replaceSelectionRequested(); });
        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(popup::kReviewSpacing);
        buttons.Children().Append(reviewKeep);
        buttons.Children().Append(reviewReplace);
        buttons.VerticalAlignment(VerticalAlignment::Bottom);

        reviewFooter = Grid();
        reviewFooter.ColumnSpacing(popup::kReviewSpacing);
        ColumnDefinition summaryColumn;
        summaryColumn.Width({1, GridUnitType::Star});
        ColumnDefinition buttonsColumn;
        buttonsColumn.Width({0, GridUnitType::Auto});
        reviewFooter.ColumnDefinitions().Append(summaryColumn);
        reviewFooter.ColumnDefinitions().Append(buttonsColumn);
        Grid::SetColumn(buttons, 1);
        reviewFooter.Children().Append(summary);
        reviewFooter.Children().Append(buttons);
        reviewCard.Children().Append(reviewFooter);

        reviewOutline = capsuleShape();
        reviewFrame = Grid();
        reviewFrame.HorizontalAlignment(HorizontalAlignment::Center);
        reviewFrame.Visibility(Visibility::Collapsed);
        reviewFrame.Children().Append(reviewOutline);
        reviewFrame.Children().Append(reviewCard);
    }

    // A push button that names the key doing what a click does, after its
    // caption in a secondary colour. Never focused: the Target keeps the
    // keyboard.
    static Button keyedButton(const QString &caption)
    {
        StackPanel parts;
        parts.Orientation(Orientation::Horizontal);
        parts.Spacing(popup::kReviewSpacing);
        TextBlock captionText;
        captionText.Text(win::hs(caption));
        parts.Children().Append(captionText);
        parts.Children().Append(TextBlock());
        Button button;
        button.Content(parts);
        button.IsTabStop(false);
        button.AllowFocusOnInteraction(false);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, win::hs(caption));
        return button;
    }

    static void setButtonKey(const Button &button, const QString &key, const Brush &ink)
    {
        const TextBlock keyText = button.Content().as<StackPanel>().Children().GetAt(1).as<TextBlock>();
        keyText.Text(win::hs(key));
        keyText.Foreground(ink);
        keyText.Visibility(key.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
    }

    // The notices float above the pill in a transparent window of their
    // own, never inside the pill's slab. Each is its own capsule, drawn as the
    // pill is, holding a plain message and an explicitly labelled accent
    // button, so the action reads as a button rather than as colored text.
    void ensureBanner()
    {
        if (banner) {
            return;
        }
        banner = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP
                | WS_EX_LAYERED,
            windowClassName, L"Speecher notices", WS_POPUP,
            0, 0, panelWidth, panelHeight, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        SetLayeredWindowAttributes(banner, 0, 255, LWA_ALPHA);

        bannerSource = DesktopWindowXamlSource();
        bannerSource.Initialize(Microsoft::UI::GetWindowIdFromWindow(banner));

        const auto accentStyle = accentButtonStyle();
        const auto makeRow = [&accentStyle](Grid &capsule, Microsoft::UI::Xaml::Shapes::Path &shape,
                                            StackPanel &row, TextBlock &message, Button &action) {
            row = StackPanel();
            row.Orientation(Orientation::Horizontal);
            row.Spacing(10);
            row.Padding({16, 5, 5, 5});
            message = TextBlock();
            message.VerticalAlignment(VerticalAlignment::Center);
            row.Children().Append(message);
            action = Button();
            action.Style(accentStyle);
            action.CornerRadius({14, 14, 14, 14});
            action.Padding({14, 4, 14, 4});
            row.Children().Append(action);
            shape = capsuleShape();
            capsule = Grid();
            capsule.HorizontalAlignment(HorizontalAlignment::Center);
            capsule.Children().Append(shape);
            capsule.Children().Append(row);
            return capsule;
        };

        bannerRoot = StackPanel();
        bannerRoot.Spacing(bannerGap);
        bannerRoot.Children().Append(
            makeRow(whatsNewCapsule, whatsNewShape, whatsNewRow, whatsNewText, whatsNewAction));
        whatsNewAction.Click([this](const auto &, const auto &) {
            emit panel->whatsNewRequested();
        });
        whatsNewDismiss = Button();
        whatsNewDismiss.Width(28);
        whatsNewDismiss.Height(28);
        whatsNewDismiss.Padding({0, 0, 0, 0});
        whatsNewDismiss.CornerRadius({14, 14, 14, 14});
        FontIcon closeIcon;
        closeIcon.Glyph(L"");
        closeIcon.FontSize(10);
        whatsNewDismiss.Content(closeIcon);
        whatsNewDismiss.Click([this](const auto &, const auto &) {
            controller->clearPendingWhatsNew();
        });
        whatsNewRow.Children().Append(whatsNewDismiss);
        bannerRoot.Children().Append(
            makeRow(updateCapsule, updateShape, updateRow, updateText, updateAction));
        updateAction.Click([this](const auto &, const auto &) {
            controller->updateBanner()->runAction();
        });
        bannerSource.Content(bannerRoot);
    }

    void refreshBanner()
    {
        UpdateBannerModel update = controller->updateBanner()->model();
        update.visible = update.visible && update.showInPopup;
        const bool showWhatsNew = !whatsNewHidden
            && !controller->pendingWhatsNewVersion().isEmpty();
        if (!window || !IsWindowVisible(window) || (!update.visible && !showWhatsNew)) {
            if (banner) {
                ShowWindow(banner, SW_HIDE);
            }
            return;
        }
        ensureBanner();
        bannerRoot.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        updateText.Text(win::hs(update.text));
        updateAction.Content(box_value(win::hs(update.action)));
        updateAction.Visibility(update.action.isEmpty() ? Visibility::Collapsed
                                                        : Visibility::Visible);
        updateAction.IsEnabled(update.actionEnabled);
        updateCapsule.Visibility(update.visible ? Visibility::Visible : Visibility::Collapsed);
        const WhatsNewBannerModel whatsNew = whatsNewBanner(controller->updates()->currentVersion());
        whatsNewText.Text(win::hs(whatsNew.text));
        whatsNewAction.Content(box_value(win::hs(whatsNew.action)));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(whatsNewDismiss,
                                                                        win::hs(whatsNew.dismiss));
        whatsNewCapsule.Visibility(showWhatsNew ? Visibility::Visible : Visibility::Collapsed);
        positionBanner();
        ShowWindow(banner, SW_SHOWNOACTIVATE);
    }

    // Centered above the pill with a small gap, sized to the measured rows,
    // each capsule's outline sized to its own row.
    void positionBanner()
    {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        const float maximumWidth = float(
            (monitor.rcWork.right - monitor.rcWork.left) / scale() - screenEdgeMargin);
        constexpr float unbounded = std::numeric_limits<float>::infinity();
        for (auto [row, shape] : {std::pair{whatsNewRow, whatsNewShape},
                                  std::pair{updateRow, updateShape}}) {
            row.Measure({maximumWidth, unbounded});
            const double rowWidth = std::ceil(row.DesiredSize().Width);
            const double rowHeight = std::ceil(row.DesiredSize().Height);
            shape.Data(capsuleGeometry(rowWidth - 1, rowHeight - 1));
            shape.Width(rowWidth);
            shape.Height(rowHeight);
        }
        bannerRoot.Measure({maximumWidth, unbounded});
        const int bannerWidth = px(int(std::ceil(bannerRoot.DesiredSize().Width)));
        const int bannerHeight = px(int(std::ceil(bannerRoot.DesiredSize().Height)));
        RECT pill{};
        GetWindowRect(window, &pill);
        const int x = pill.left + (pill.right - pill.left - bannerWidth) / 2;
        const int y = pill.bottom - px(contentHeight()) - bannerHeight - px(bannerGap);
        SetWindowPos(banner, HWND_TOPMOST, x, y, bannerWidth, bannerHeight,
                     SWP_NOACTIVATE);
        bannerSource.SiteBridge().MoveAndResize({0, 0, bannerWidth, bannerHeight});
    }

    void show(quint64 generation)
    {
        // A dictation starting inside a problem's countdown must not be torn
        // down when that problem's timer fires.
        problemAutoDismiss.stop();
        countdownTick.stop();
        pausedRemainingMs = 0;
        problem.clear();
        fix = {};
        // The previous dictation's words are spent; the session's clearing
        // preview can be dropped by the frozen guard, so clear here too.
        preview.clear();
        completed = false;
        phase = Phase::Live;
        pendingGeneration = generation;
        ensureWindow();
        wave->restart();
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

    void showProblem(const QString &message, const PopupErrorAction &errorFix)
    {
        preview.clear();
        problem = message;
        fix = fixAvailable(errorFix) ? errorFix : PopupErrorAction{};
        pendingGeneration = 0;
        ensureWindow();
        applyTheme();
        whatsNewHidden = false;
        const int dismissMs = popupErrorDismissMs(message);
        pausedRemainingMs = 0;
        problemAutoDismiss.start(dismissMs);
        countdown.Maximum(dismissMs);
        countdown.Value(dismissMs);
        countdownTick.start();
        refresh();
        reposition();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        if (const auto peer = Microsoft::UI::Xaml::Automation::Peers::FrameworkElementAutomationPeer::
                CreatePeerForElement(text)) {
            peer.RaiseAutomationEvent(Microsoft::UI::Xaml::Automation::Peers::AutomationEvents::LiveRegionChanged);
        }
        refreshBanner();
        if (!controller->pendingWhatsNewVersion().isEmpty()) {
            whatsNewAutoHide.start();
        }
    }

    // The same review comes again as a follow-up starts and ends; it keeps
    // its Show all and its scrolling then, and a new one starts folded at the
    // top.
    void showReview(const SelectionEditReview &value)
    {
        const bool fresh = !reviewShown;
        review = value;
        reviewShown = true;
        if (fresh) {
            reviewWhole = false;
        }
        ensureWindow();
        applyTheme();
        fillReview();
        if (fresh) {
            reviewScroll.ChangeView(nullptr, 0.0, nullptr, true);
        }
        refresh();
        reposition();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        refreshBanner();
    }

    // Fills the card with the review, folded or whole, in the panel's theme.
    void fillReview()
    {
        win::PaneHost theme;
        theme.effectiveTheme = [this] { return reviewFrame.ActualTheme(); };
        const Brush secondary = win::themeBrush(L"SettingsCardDescriptionForeground", theme);
        reviewInstruction.Text(win::hs(review.instruction));
        reviewInstruction.Foreground(secondary);
        reviewInstruction.Visibility(review.instruction.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
        const bool folded = !review.folded.isEmpty() && !reviewWhole;
        fillEdit(folded ? review.folded : review.runs, secondary, theme);
        reviewLaidOutWidth = 0;
        // Dimmed while a follow-up is dictated: the edit is about to change.
        reviewScroll.Opacity(review.following ? popup::kFollowUpEditOpacity : 1.0);
        reviewFooter.Visibility(review.following ? Visibility::Collapsed : Visibility::Visible);
        reviewSummary.Text(win::hs(review.summary));
        reviewSummary.Foreground(secondary);
        reviewHint.Text(win::hs(review.followUpHint));
        reviewHint.Foreground(secondary);
        reviewHint.Visibility(review.followUpHint.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
        reviewToggle.Content(box_value(win::hs(reviewWhole ? showChangesOnlyCaption() : showWholeEditCaption())));
        reviewToggle.Visibility(review.folded.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
        setButtonKey(reviewKeep, review.keys.keep, secondary);
        // Replace is the default button where Enter presses it.
        const bool replaceIsDefault = !review.keys.replace.isEmpty();
        if (replaceIsDefault) {
            reviewReplace.Style(accentButtonStyle());
        } else {
            reviewReplace.ClearValue(FrameworkElement::StyleProperty());
        }
        setButtonKey(reviewReplace, review.keys.replace,
                     replaceIsDefault ? win::themeBrush(L"AccentButtonSecondaryForeground", theme) : secondary);
    }

    // The edit as runs: added words over a fifth of the success colour,
    // removed ones struck through in the secondary colour over a fifth of the
    // critical colour, and left-out words as a secondary ellipsis. A run's
    // trailing spaces stay unmarked. A Run has no background of its own, so
    // the tints are text highlighters, whose ranges count the characters of
    // every run together.
    void fillEdit(const QList<EditRun> &runs, const Brush &secondary, const win::PaneHost &theme)
    {
        namespace Documents = Microsoft::UI::Xaml::Documents;
        reviewEdit.Inlines().Clear();
        reviewEdit.TextHighlighters().Clear();
        // A highlighter without a foreground paints its words in the system's
        // highlight colour, so each keeps the colour its words already have.
        Documents::TextHighlighter added;
        added.Background(editTint(win::themeBrush(L"PositiveTextForeground", theme)));
        added.Foreground(SolidColorBrush(solidColor(reviewEdit.Foreground())));
        Documents::TextHighlighter removed;
        removed.Background(editTint(win::themeBrush(L"NegativeTextForeground", theme)));
        removed.Foreground(SolidColorBrush(solidColor(secondary)));
        int32_t position = 0;
        for (const EditRun &run : runs) {
            Documents::Run words;
            words.Text(win::hs(run.text));
            const Documents::TextRange range{position, int32_t(run.text.size())};
            switch (run.kind) {
            case EditRun::Kind::Kept:
                break;
            case EditRun::Kind::Added:
                added.Ranges().Append(range);
                break;
            case EditRun::Kind::Removed:
                words.Foreground(secondary);
                words.TextDecorations(winrt::Windows::UI::Text::TextDecorations::Strikethrough);
                removed.Ranges().Append(range);
                break;
            case EditRun::Kind::Omitted:
                words.Foreground(secondary);
                break;
            }
            reviewEdit.Inlines().Append(words);
            if (!run.trailing.isEmpty()) {
                Documents::Run trailing;
                trailing.Text(win::hs(run.trailing));
                reviewEdit.Inlines().Append(trailing);
            }
            position += int32_t(run.text.size() + run.trailing.size());
        }
        reviewEdit.TextHighlighters().Append(added);
        reviewEdit.TextHighlighters().Append(removed);
    }

    void hide()
    {
        whatsNewAutoHide.stop();
        problemAutoDismiss.stop();
        countdownTick.stop();
        pausedRemainingMs = 0;
        if (wave) {
            wave->setRunning(false);
        }
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
            panelStack.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
            // The rectangles captured the text brush at creation; a theme
            // change hands them the newly resolved one. While the status text
            // shimmers its foreground is the animated gradient, which would
            // freeze into the bars as a half-swept smear.
            wave->setInk(shimmering ? normalForeground : text.Foreground());
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
        wave->setLevel(value);
    }

    // The caution colour a paused dictation's bars lie flat in, for the
    // panel's current theme.
    Brush pausedFill() const
    {
        win::PaneHost theme;
        theme.effectiveTheme = [this] { return chrome.ActualTheme(); };
        return win::themeBrush(L"PausedForeground", theme);
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
        const auto color = solidColor(normalForeground);
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
        if (reviewShown && !review.following) {
            refreshReview();
            return;
        }
        // The capsule, under the review card while a follow-up is dictated.
        chrome.Visibility(Visibility::Visible);
        reviewFrame.Visibility(reviewShown ? Visibility::Visible : Visibility::Collapsed);
        const bool hasProblem = !problem.isEmpty();
        // A finished delivery: the outcome message is the whole story, so the
        // spent preview words go and the icon and message centre in the pill.
        const bool finished = completed && !hasProblem;
        const SessionControls controls = hasProblem || finished ? SessionControls{}
                                                                : sessionControls(sessionState);
        // The spinner takes pause's slot, so either one fills the left of the row.
        const bool leftSlotVisible = controls.pauseVisible || controls.busyVisible;
        const int controlsWidth = (leftSlotVisible ? sessionButtonSize + sessionButtonGap : 0)
            + (controls.cancelVisible ? sessionButtonSize + sessionButtonGap : 0);
        const bool paused = controls.paused;
        // An outcome with a fix is laid out like an error, without Dismiss or
        // the countdown: its line wraps and its capsule grows taller.
        const bool offersFix = (hasProblem || finished) && fix.fix != ErrorFix::None;
        const bool wraps = hasProblem || offersFix;
        // Only errors, a fix and the session buttons take clicks. Otherwise the
        // transparent space around a live capsule must not intercept clicks in
        // the target application.
        const bool interactive = hasProblem || offersFix || controlsWidth > 0;
        setTakesClicks(interactive);
        // A receipt outranks the refining flag, which can still be set when
        // the delivery lands.
        glyph.Glyph(hstring((finished                   ? outcomeGlyph(outcome)
                             : refining && !hasProblem ? QString::fromUtf16(u"\uE8A9")
                                                       : phaseGlyph(status, hasProblem))
                                .toStdWString()));
        const bool renewing = status == renewingSignInText();
        const bool waiting = !hasProblem && !finished && (phase != Phase::Live || renewing);
        // The bars show while listening and, flat in the caution colour, while
        // paused.
        const bool listening = !hasProblem && !finished && !waiting;
        const bool showPreview = !hasProblem && !finished && !preview.isEmpty();
        setShimmer(waiting);
        QString shown = hasProblem ? problem : finished ? status
            : renewing ? status : phase == Phase::Transcribing ? dictationStatusLabel(QStringLiteral("stopping"))
            : waiting ? dictationStatusLabel(QStringLiteral("refining")) : QString();
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        const int screenWidth =
            int((monitor.rcWork.right - monitor.rcWork.left) / scale()) - screenEdgeMargin;
        const int chromeWidth = (hasProblem ? problemChromeWidth : receiptChromeWidth)
            + (offersFix ? measuredTextWidth(popupErrorActionLabel(fix)) + buttonChromeWidth : 0);
        // Measure the native font so both the contour and strip clear its ink.
        probe.FontSize(previewFontSize);
        probe.Text(L"Ag");
        probe.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
        const int lineHeight = int(std::ceil(probe.DesiredSize().Height));
        // The status beside the buttons hugs its text, so it sits centred
        // between them.
        const int statusWidth = waiting ? measuredTextWidth(shown, previewFontSize) : 0;
        // The lobe holds the whole row: the bars or the status, with a button
        // either side when they show.
        const double inkWidth = (listening ? wave->stripWidth() : statusWidth) + controlsWidth;
        const int lobeWidth = int(std::ceil(inkWidth)) + 2 * popup::kLobeAir;
        // A problem wraps at the width every platform shares and grows taller.
        const int maximumWidth = wraps
            ? std::min(kPopupErrorWrapWidth + chromeWidth, screenWidth)
            : std::max(problemMinimumWidth, screenWidth);
        int wantedWidth = hasProblem ? std::clamp(measuredTextWidth(shown) + chromeWidth,
                                                  problemMinimumWidth, maximumWidth)
            : finished ? std::clamp(measuredTextWidth(shown) + chromeWidth, panelWidth, maximumWidth)
            : controlsWidth > 0 ? lobeWidth
            : waiting ? std::max(panelWidth, statusWidth + 32)
                      : panelWidth;
        if (showPreview) {
            const int transcriptMaximum = std::min(maximumPreviewWidth, maximumWidth);
            const PreviewLine visible = fitPreview(preview, transcriptMaximum - previewChromeWidth);
            // A short preview still carves the text bar around the lobe,
            // centred in the narrowest bar that does.
            const int shortestBar = int(std::ceil(popup::minimumPreviewBarWidth(
                lobeWidth, previewTopPadding + lineHeight + previewShoulderDrop)));
            // Match macOS: hug the transcript until it reaches the width cap.
            // Measuring the trimmed tail would make the width twitch at overflow.
            wantedWidth = std::min(transcriptMaximum,
                std::max(shortestBar, measuredTextWidth(preview, previewFontSize) + previewChromeWidth));
            // The line hugs the words it shows and centres in the bar, so its
            // left edge is the text's own and the fade starts there.
            previewText.Text(hstring(visible.text.toStdWString()));
            previewText.Width(measuredTextWidth(visible.text, previewFontSize));
            setPreviewFade(visible.cut);
        }
        previewText.Visibility(showPreview ? Visibility::Visible : Visibility::Collapsed);
        row.Spacing(hasProblem || finished ? messageRowSpacing : sessionButtonGap);
        row.Padding(hasProblem || finished ? Thickness{12, 0, 12, 0} : Thickness{});
        row.Margin({0, showPreview ? double(previewStripSpacing) : 0.0, 0, 0});
        content.Padding({0, 0, 0, showPreview ? double(previewBottomPadding) : 0.0});
        text.Text(hstring(shown.toStdWString()));
        text.FontSize(waiting ? previewFontSize : bodyFontSize);
        text.Visibility(listening ? Visibility::Collapsed : Visibility::Visible);
        text.Width(hasProblem || finished ? wantedWidth - chromeWidth
                   : controlsWidth > 0 ? statusWidth
                                       : wantedWidth);
        text.TextWrapping(wraps ? TextWrapping::Wrap : TextWrapping::NoWrap);
        text.MaxLines(wraps ? 0 : 1);
        text.TextAlignment(TextAlignment::Center);
        row.HorizontalAlignment(HorizontalAlignment::Center);
        glyph.Visibility(hasProblem || finished ? Visibility::Visible : Visibility::Collapsed);
        waveform.Visibility(listening ? Visibility::Visible : Visibility::Collapsed);
        // Beside the buttons the strip is only as wide as its dots, so the tab
        // under the words hugs [button][bars][button] with equal gaps.
        waveform.Width(controlsWidth > 0 ? wave->stripWidth() : double(panelWidth));
        pauseButton.Visibility(controls.pauseVisible ? Visibility::Visible : Visibility::Collapsed);
        pauseButton.IsEnabled(controls.pauseEnabled);
        pauseButton.Content().as<FontIcon>().Glyph(paused ? L"\uE768" : L"\uE769");
        const QString pauseText = paused ? resumeCaption() : pauseCaption();
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(pauseButton, win::hs(pauseText));
        ToolTipService::SetToolTip(pauseButton, box_value(win::hs(pauseText)));
        busySlot.Visibility(controls.busyVisible ? Visibility::Visible : Visibility::Collapsed);
        // A hidden spinner stops, as a hidden strip does.
        busySlot.Child().as<ProgressRing>().IsActive(controls.busyVisible);
        cancelButton.Visibility(controls.cancelVisible ? Visibility::Visible : Visibility::Collapsed);
        wave->setFrozen(frozen);
        wave->setPaused(paused, paused ? pausedFill() : Brush{nullptr});
        wave->setRunning(listening);
        const QString waveName = paused ? dictationStatusLabel(QStringLiteral("paused")) : inputLevelLabel();
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(wave->element(), win::hs(waveName));
        ToolTipService::SetToolTip(waveform, paused ? box_value(win::hs(waveName)) : nullptr);
        dismiss.Visibility(hasProblem ? Visibility::Visible : Visibility::Collapsed);
        fixButton.Visibility(offersFix ? Visibility::Visible : Visibility::Collapsed);
        if (offersFix) {
            fixButton.Content(box_value(win::hs(popupErrorActionLabel(fix))));
        }
        countdown.Visibility(hasProblem ? Visibility::Visible : Visibility::Collapsed);
        int messageHeight = 0;
        if (wraps) {
            text.Measure({float(wantedWidth - chromeWidth),
                          std::numeric_limits<float>::infinity()});
            messageHeight = std::max(hasProblem ? problemMinimumHeight - problemBarHeight : panelHeight,
                                     int(std::ceil(text.DesiredSize().Height)) + 2 * problemPadding);
        }
        // Under the words the strip is still as tall as the buttons beside it.
        const int buttonRowHeight = controlsWidth > 0 ? sessionButtonSize : 0;
        const int stripHeight = wraps ? messageHeight
            : showPreview ? std::max(buttonRowHeight, waiting ? lineHeight + 6 : compactStripHeight)
                          : panelHeight;
        row.Height(stripHeight);
        wave->element().Height(stripHeight);
        const int wantedHeight = hasProblem ? messageHeight + problemBarHeight
            : wraps ? messageHeight
            : showPreview
            ? previewTopPadding + lineHeight + previewStripSpacing + stripHeight + previewBottomPadding
            : panelHeight;
        // Keep the native host stable while XAML resizes the visible capsule.
        // Resizing the HWND first can clip the previous composition frame.
        surfaceWidth = wraps ? wantedWidth : std::max(wantedWidth, std::min(maximumPreviewWidth, maximumWidth));
        surfaceHeight = wraps ? wantedHeight
            : previewTopPadding + lineHeight + previewStripSpacing
                + std::max({compactStripHeight, sessionButtonSize, lineHeight + 6}) + previewBottomPadding;
        if (reviewShown) {
            const int cardWidth = reviewWidth();
            layOutReviewCard(cardWidth);
            reviewFrame.Margin({0, 0, 0, bannerGap});
            surfaceWidth = std::max(surfaceWidth, cardWidth);
            surfaceHeight += reviewHeight + bannerGap;
        }
        resize(wantedWidth, wantedHeight);
        updateOutline(showPreview ? previewTopPadding + lineHeight + previewShoulderDrop : 0, inkWidth);
        chrome.UpdateLayout();
        if (IsWindowVisible(window)) {
            reposition();
        }
        limitClicksToCapsule();
        refreshBanner();
    }

    // The review card alone, in the capsule's place.
    void refreshReview()
    {
        setShimmer(false);
        wave->setRunning(false);
        setTakesClicks(true);
        chrome.Visibility(Visibility::Collapsed);
        reviewFrame.Visibility(Visibility::Visible);
        reviewFrame.Margin({});
        const int cardWidth = reviewWidth();
        layOutReviewCard(cardWidth);
        surfaceWidth = cardWidth;
        surfaceHeight = reviewHeight;
        resizeSurface();
        panelStack.UpdateLayout();
        if (IsWindowVisible(window)) {
            reposition();
        }
        limitClicksToCapsule();
        refreshBanner();
    }

    // The card's width: the shared one, narrower on a screen too small for
    // it, as a problem wraps narrower there.
    int reviewWidth() const
    {
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        return std::min(reviewCardWidth,
                        int((monitor.rcWork.right - monitor.rcWork.left) / scale()) - screenEdgeMargin);
    }

    // Sizes the card to this width and as tall as it measures, the edit
    // scrolling past its line limit, with its outline around it. Only when
    // the review or the width changed: while a follow-up is dictated the
    // panel refreshes with every word, and the card stays as it was.
    void layOutReviewCard(int cardWidth)
    {
        if (cardWidth == reviewLaidOutWidth) {
            return;
        }
        reviewLaidOutWidth = cardWidth;
        constexpr float unbounded = std::numeric_limits<float>::infinity();
        probe.FontSize(previewFontSize);
        probe.Text(L"Ag");
        probe.Measure({unbounded, unbounded});
        reviewScroll.MaxHeight(std::ceil(probe.DesiredSize().Height) * popup::kReviewMaxLines);
        reviewCard.Width(cardWidth);
        reviewCard.Measure({float(cardWidth), unbounded});
        reviewHeight = int(std::ceil(reviewCard.DesiredSize().Height));
        // Inside its half-DIP margin, so the frame is exactly the card's size.
        reviewOutline.Data(capsuleGeometry(cardWidth - 1, reviewHeight - 1));
        reviewOutline.Width(cardWidth - 1);
        reviewOutline.Height(reviewHeight - 1);
    }

    // Clicks reach the panel's controls, or pass through it to the
    // application below.
    void setTakesClicks(bool value)
    {
        takesClicks = value;
        const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        const LONG_PTR wantedStyle = takesClicks ? style & ~WS_EX_TRANSPARENT : style | WS_EX_TRANSPARENT;
        if (style != wantedStyle) {
            SetWindowLongPtrW(window, GWL_EXSTYLE, wantedStyle);
        }
    }

    // The surface is sized for the widest preview, so while the panel takes
    // clicks the empty band beside the capsule would swallow clicks meant for
    // the application below. A window region keeps hit testing, child island
    // included, to the capsule's rectangle, and to the review card's above it.
    // The region clips drawing too, so it holds every pixel of the outlines,
    // strokes and fillets included.
    void limitClicksToCapsule()
    {
        // The words bar across the top, and under it only the tab's width
        // when the outline carves one, so clicks beside the tab go through,
        // plus the band where the fillets turn from the shoulder into the tab.
        RECT bar{};
        RECT tab{};
        RECT joins{};
        RECT card{};
        const auto px = [this](double dip) { return int(std::lround(dip * scale())); };
        const auto boundsOf = [](const FrameworkElement &element) {
            return element.TransformToVisual(nullptr).TransformBounds(
                {0, 0, float(element.ActualWidth()), float(element.ActualHeight())});
        };
        if (takesClicks && reviewFrame.Visibility() == Visibility::Visible) {
            const auto bounds = boundsOf(reviewFrame);
            card = {px(bounds.X), px(bounds.Y), px(bounds.X + bounds.Width), px(bounds.Y + bounds.Height)};
        }
        if (takesClicks && chrome.Visibility() == Visibility::Visible) {
            const auto bounds = boundsOf(chrome);
            bar = {px(bounds.X), px(bounds.Y), px(bounds.X + bounds.Width), px(bounds.Y + bounds.Height)};
            if (outlineTabHalf > 0) {
                const double middle = bounds.X + bounds.Width / 2.0;
                // The outline's one-DIP stroke reaches past the shoulder and
                // the tab's sides.
                const double shoulder = bounds.Y + outlineShoulder + 1;
                const double half = outlineTabHalf + 1;
                tab = {px(middle - half), px(shoulder), px(middle + half), bar.bottom};
                joins = {px(middle - half - outlineFillet), px(shoulder),
                         px(middle + half + outlineFillet), px(shoulder + outlineFillet)};
                bar.bottom = tab.top;
            }
        }
        if (EqualRect(&bar, &clickRegion) && EqualRect(&tab, &clickTab)
            && EqualRect(&joins, &clickJoins) && EqualRect(&card, &clickCard)) {
            return;
        }
        clickRegion = bar;
        clickTab = tab;
        clickJoins = joins;
        clickCard = card;
        HRGN region = nullptr;
        if (takesClicks) {
            region = CreateRectRgn(0, 0, 0, 0);
            for (const RECT &part : {bar, tab, joins, card}) {
                if (!IsRectEmpty(&part)) {
                    HRGN extra = CreateRectRgnIndirect(&part);
                    CombineRgn(region, region, extra, RGN_OR);
                    DeleteObject(extra);
                }
            }
        }
        // The system owns the region once it is set.
        SetWindowRgn(window, region, IsWindowVisible(window));
    }

    bool pointerOverChrome() const
    {
        POINT pointer{};
        RECT surface{};
        if (!chrome || !GetCursorPos(&pointer) || !GetWindowRect(window, &surface)) {
            return false;
        }
        const auto bounds = chrome.TransformToVisual(nullptr).TransformBounds(
            {0, 0, float(chrome.ActualWidth()), float(chrome.ActualHeight())});
        const double x = (pointer.x - surface.left) / scale();
        const double y = (pointer.y - surface.top) / scale();
        return x >= bounds.X && x < bounds.X + bounds.Width && y >= bounds.Y
            && y < bounds.Y + bounds.Height;
    }

    double scale() const
    {
        return window ? GetDpiForWindow(window) / 96.0 : 1.0;
    }

    int px(int dip) const
    {
        return int(dip * scale() + 0.5);
    }

    // Desired width of the line at this font size, the way the mac panel
    // measures its NSString. A detached TextBlock measures fine; if XAML
    // ever hands back nothing, the 7px-per-character estimate stands in.
    int measuredTextWidth(const QString &value, double fontSize = bodyFontSize)
    {
        probe.FontSize(fontSize);
        probe.Text(hstring(value.toStdWString()));
        constexpr float unbounded = std::numeric_limits<float>::infinity();
        probe.Measure({unbounded, unbounded});
        const int measured = int(std::ceil(probe.DesiredSize().Width));
        return measured > 0 ? measured + 2 : int(value.size()) * 7;
    }

    PreviewLine fitPreview(const QString &value, int maximumWidth)
    {
        return trimPreviewToFit(value, [this, maximumWidth](const QString &candidate) {
            return measuredTextWidth(candidate, previewFontSize) <= maximumWidth;
        });
    }

    // Words cut from the front fade in over the line's first stretch, so the
    // newest words stay whole. The line hugs its text, so the fade starts at
    // the text's left edge whether WinUI maps a text brush to the TextBlock
    // or to the glyphs. Rebuilt on every refresh from the theme's text
    // colour, so it follows a theme change as the plain foreground does.
    void setPreviewFade(bool cut)
    {
        previewText.ClearValue(TextBlock::ForegroundProperty());
        if (!cut) {
            return;
        }
        const auto color = solidColor(previewText.Foreground());
        auto clear = color;
        clear.A = 0;
        LinearGradientBrush fade;
        fade.MappingMode(BrushMappingMode::Absolute);
        fade.StartPoint({0, 0});
        fade.EndPoint({float(popup::kPreviewFadeWidth), 0});
        for (const auto &[shade, offset] : {std::pair{clear, 0.0}, std::pair{color, 1.0}}) {
            GradientStop stop;
            stop.Color(shade);
            stop.Offset(offset);
            fade.GradientStops().Append(stop);
        }
        previewText.Foreground(fade);
    }

    // The pill's own surface: the theme's acrylic fill and strong stroke,
    // which the banner capsules share.
    static Microsoft::UI::Xaml::Shapes::Path capsuleShape()
    {
        auto shape = Microsoft::UI::Xaml::Markup::XamlReader::Load(
            LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Fill="{ThemeResource AcrylicBackgroundFillColorDefaultBrush}" Stroke="{ThemeResource ControlStrongStrokeColorDefaultBrush}" StrokeThickness="1"/>)")
            .as<Microsoft::UI::Xaml::Shapes::Path>();
        shape.Margin({0.5, 0.5, 0.5, 0.5});
        shape.HorizontalAlignment(HorizontalAlignment::Left);
        shape.VerticalAlignment(VerticalAlignment::Top);
        return shape;
    }

    // A rounded rectangle whose corners are a capsule's at the pill's height.
    static PathGeometry capsuleGeometry(double width, double height)
    {
        const double radius = std::min(24.0, height / 2.0);
        PathFigure figure;
        figure.IsClosed(true);
        const auto line = [&](double x, double y) {
            LineSegment segment;
            segment.Point({float(x), float(y)});
            figure.Segments().Append(segment);
        };
        const auto arc = [&](double x, double y) {
            ArcSegment segment;
            segment.Point({float(x), float(y)});
            segment.Size({float(radius), float(radius)});
            segment.SweepDirection(SweepDirection::Clockwise);
            figure.Segments().Append(segment);
        };
        figure.StartPoint({float(radius), 0});
        line(width - radius, 0);
        arc(width, radius);
        line(width, height - radius);
        arc(width - radius, height);
        line(radius, height);
        arc(0, height - radius);
        line(0, radius);
        arc(radius, 0);
        PathGeometry geometry;
        geometry.Figures().Append(figure);
        return geometry;
    }

    // Same circular end caps and concave joins as the accepted Qt preview.
    void updateOutline(double shoulder, double inkWidth)
    {
        // Keep the whole stroke inside the HWND rather than clipping its edges.
        const double width = this->width - 1;
        const double height = this->height - 1;
        const double cap = shoulder / 2;
        const double half = inkWidth / 2 + popup::kLobeAir;
        const double left = width / 2.0 - half;
        const double right = width / 2.0 + half;
        const double lobeHeight = height - shoulder;
        double fillet = std::min(popup::kFillet, left - cap);
        double radius = std::min(popup::kLobeRadius, half);
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
            outlineShoulder = 0;
            outlineTabHalf = 0;
            outlineFillet = 0;
            outline.Data(capsuleGeometry(width, height));
            return;
        }
        outlineShoulder = shoulder;
        outlineTabHalf = half;
        outlineFillet = fillet;
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
        resizeSurface();
    }

    void resizeSurface()
    {
        if (source) {
            source.SiteBridge().MoveAndResize({0, 0, px(surfaceWidth), px(surfaceHeight)});
        }
    }

    // How far the panel's content reaches up from the window's bottom edge:
    // the capsule, the review card, or the card above the capsule.
    int contentHeight() const
    {
        if (!reviewShown) {
            return height;
        }
        return review.following ? reviewHeight + bannerGap + height : reviewHeight;
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
        // The width layout gave the element: a TextBlock's ActualWidth is its
        // text's, short of the explicit Width the row arranged it in.
        const double width = std::isnan(control.Width()) ? control.ActualWidth() : control.Width();
        return QRect(bounds.left + qRound(point.X * scale()),
                     bounds.top + qRound(point.Y * scale()),
                     qRound(width * scale()),
                     qRound(control.ActualHeight() * scale()));
    }

    ApplicationController *controller;
    DictationPanel *panel;
    HWND window = nullptr;
    TextBlock previewText{nullptr};
    Border waveform{nullptr};
    Button pauseButton{nullptr};
    // The spinner in pause's slot while transcribing and refining.
    Border busySlot{nullptr};
    Button cancelButton{nullptr};
    // The session's state name, from its stateChanged signal.
    QString sessionState;
    HWND banner = nullptr;
    DesktopWindowXamlSource source{nullptr};
    DesktopWindowXamlSource bannerSource{nullptr};
    Border chrome{nullptr};
    Microsoft::UI::Xaml::Shapes::Path outline{nullptr};
    StackPanel content{nullptr};
    StackPanel bannerRoot{nullptr};
    Grid updateCapsule{nullptr};
    Microsoft::UI::Xaml::Shapes::Path updateShape{nullptr};
    StackPanel updateRow{nullptr};
    TextBlock updateText{nullptr};
    Button updateAction{nullptr};
    Grid whatsNewCapsule{nullptr};
    Microsoft::UI::Xaml::Shapes::Path whatsNewShape{nullptr};
    StackPanel whatsNewRow{nullptr};
    TextBlock whatsNewText{nullptr};
    Button whatsNewAction{nullptr};
    Button whatsNewDismiss{nullptr};
    QTimer whatsNewAutoHide;
    bool whatsNewHidden = false;
    FontIcon glyph{nullptr};
    TextBlock text{nullptr};
    TextBlock probe{nullptr};
    win::WaveformBars *wave = nullptr;
    QTimer problemAutoDismiss;
    QTimer countdownTick;
    // What the countdown had left when the pointer arrived; 0 while it runs.
    int pausedRemainingMs = 0;
    ProgressBar countdown{nullptr};
    Button fixButton{nullptr};
    Button dismiss{nullptr};
    // The review card above the capsule (chrome), standing on the window's
    // bottom edge.
    StackPanel panelStack{nullptr};
    // The card and its outline.
    Grid reviewFrame{nullptr};
    Microsoft::UI::Xaml::Shapes::Path reviewOutline{nullptr};
    StackPanel reviewCard{nullptr};
    TextBlock reviewInstruction{nullptr};
    ScrollViewer reviewScroll{nullptr};
    TextBlock reviewEdit{nullptr};
    TextBlock reviewSummary{nullptr};
    HyperlinkButton reviewToggle{nullptr};
    TextBlock reviewHint{nullptr};
    // The summary, the hint and the buttons, gone while following up.
    Grid reviewFooter{nullptr};
    Button reviewKeep{nullptr};
    Button reviewReplace{nullptr};
    SelectionEditReview review;
    // The card shows from a review's first arrival until the session ends
    // it, through any follow-up.
    bool reviewShown = false;
    // The card's height as layOutReviewCard() measured it, and the width it
    // measured at; 0 when the card has changed since.
    int reviewHeight = 0;
    int reviewLaidOutWidth = 0;
    // Show all: the whole edit instead of the folded one.
    bool reviewWhole = false;
    PopupErrorAction fix;
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
    // Whether the panel takes clicks, which limitClicksToCapsule() keeps to
    // the capsule.
    bool takesClicks = false;
    // The window region limitClicksToCapsule() last set, as the words bar,
    // the tab under it and the fillets' band; empty for none.
    RECT clickRegion{};
    RECT clickTab{};
    RECT clickJoins{};
    // The review card's part of that region.
    RECT clickCard{};
    // The carved outline's shoulder, the tab's half width and the fillet's
    // radius in DIPs, which the click region follows; 0 while the outline is
    // a plain capsule.
    double outlineShoulder = 0;
    double outlineTabHalf = 0;
    double outlineFillet = 0;
    bool shimmering = false;
    bool frozen = false;
    bool completed = false;
    PopupOutcome outcome = PopupOutcome::Inserted;
    bool refining = false;
    bool loaded = false;
};

DictationPanel::DictationPanel(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<Native>(controller, this))
{
}

DictationPanel::~DictationPanel() = default;

void DictationPanel::showProblem(const QString &message, const PopupErrorAction &fix)
{
    m_native->showProblem(message, fix);
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

bool DictationPanel::fixVisibleForTest() const
{
    return m_native->fixButton && m_native->fixButton.Visibility() == Visibility::Visible;
}

bool DictationPanel::errorChromeVisibleForTest() const
{
    return (m_native->dismiss && m_native->dismiss.Visibility() == Visibility::Visible)
        || (m_native->countdown && m_native->countdown.Visibility() == Visibility::Visible);
}

void DictationPanel::pressFixForTest()
{
    winrt::Microsoft::UI::Xaml::Automation::Peers::ButtonAutomationPeer(m_native->fixButton).Invoke();
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
    return m_native->wave ? m_native->wave->count() : 0;
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

QRect DictationPanel::statusGeometryForTest() const
{
    return m_native->controlGeometry(m_native->text);
}

QRect DictationPanel::pauseGeometryForTest() const
{
    return m_native->controlGeometry(m_native->pauseButton);
}

QRect DictationPanel::spinnerGeometryForTest() const
{
    return m_native->controlGeometry(m_native->busySlot);
}

QRect DictationPanel::cancelGeometryForTest() const
{
    return m_native->controlGeometry(m_native->cancelButton);
}

QString DictationPanel::previewTextForTest() const
{
    return QString::fromStdWString(std::wstring(m_native->previewText.Text()));
}

bool DictationPanel::previewTextFitsForTest() const
{
    return m_native->measuredTextWidth(previewTextForTest(), previewFontSize)
        <= m_native->chrome.ActualWidth() - previewChromeWidth;
}

bool DictationPanel::previewFadesForTest() const
{
    return m_native->previewText.Foreground().try_as<LinearGradientBrush>() != nullptr;
}

double DictationPanel::outlineShoulderForTest() const
{
    return m_native->outlineShoulder;
}

double DictationPanel::outlineLobeWidthForTest() const
{
    return 2 * m_native->outlineTabHalf;
}

QRect DictationPanel::reviewGeometryForTest() const
{
    return m_native->controlGeometry(m_native->reviewFrame);
}

bool DictationPanel::reviewFullyVisibleForTest() const
{
    const QRect card = reviewGeometryForTest();
    RECT window{};
    if (card.isEmpty() || !GetWindowRect(m_native->window, &window)) {
        return false;
    }
    QRect shown(window.left, window.top, window.right - window.left, window.bottom - window.top);
    // A window that takes no clicks has no region clipping it. The region is
    // in the window's own coordinates.
    if (RECT region{}; GetWindowRgnBox(m_native->window, &region) != ERROR) {
        shown &= QRect(window.left + region.left, window.top + region.top, region.right - region.left,
                       region.bottom - region.top);
    }
    // A pixel either way is rounding between the two measures.
    shown.adjust(-1, -1, 1, 1);
    const QRect capsule = capsuleGeometryForTest();
    return shown.contains(card) && (capsule.isEmpty() || shown.contains(capsule));
}

bool DictationPanel::reviewFollowingForTest() const
{
    return m_native->reviewFooter.Visibility() == Visibility::Collapsed
        && m_native->reviewScroll.Opacity() < 1.0;
}

QString DictationPanel::reviewHintForTest() const
{
    const TextBlock hint = m_native->reviewHint;
    return hint.Visibility() == Visibility::Collapsed ? QString()
                                                      : QString::fromStdWString(std::wstring(hint.Text()));
}

QString DictationPanel::reviewTextForTest() const
{
    QString text;
    for (const auto &piece : m_native->reviewEdit.Inlines()) {
        if (const auto run = piece.try_as<winrt::Microsoft::UI::Xaml::Documents::Run>()) {
            text += QString::fromStdWString(std::wstring(run.Text()));
        }
    }
    return text;
}

QString DictationPanel::reviewToggleForTest() const
{
    const HyperlinkButton toggle = m_native->reviewToggle;
    if (toggle.Visibility() == Visibility::Collapsed) {
        return {};
    }
    return QString::fromStdWString(std::wstring(unbox_value<hstring>(toggle.Content())));
}

bool DictationPanel::reviewScrollsForTest() const
{
    return m_native->reviewScroll.ScrollableHeight() > 0;
}

bool DictationPanel::reviewReplaceIsDefaultForTest() const
{
    return m_native->reviewReplace.Style() == accentButtonStyle();
}

void DictationPanel::pressReviewToggleForTest()
{
    winrt::Microsoft::UI::Xaml::Automation::Peers::HyperlinkButtonAutomationPeer(m_native->reviewToggle).Invoke();
}

void DictationPanel::pressKeepOriginalForTest()
{
    winrt::Microsoft::UI::Xaml::Automation::Peers::ButtonAutomationPeer(m_native->reviewKeep).Invoke();
}

void DictationPanel::pressReplaceForTest()
{
    winrt::Microsoft::UI::Xaml::Automation::Peers::ButtonAutomationPeer(m_native->reviewReplace).Invoke();
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
    // The capsule, the review card, or both while a follow-up is dictated.
    const QRect shown = capsuleGeometryForTest() | reviewGeometryForTest();
    RECT rect{shown.left(), shown.top(), shown.x() + shown.width(), shown.y() + shown.height()};
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
