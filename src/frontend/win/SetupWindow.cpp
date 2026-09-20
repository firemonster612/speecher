#include "frontend/win/SetupWindow.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/AppSettings.h"
#include "core/OutputFormat.h"
#include "core/SettingsStore.h"
#include "core/ShortcutBinding.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "frontend/win/SettingsPage.h"
#include "frontend/win/ShortcutRecorder.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "providers/ProviderRegistry.h"

#include <windows.h>
#include <shellapi.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <QDebug>
#include <QHash>
#include <QKeySequence>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using Microsoft::UI::Xaml::Markup::XamlReader;

// Every XAML fragment this file parses carries the presentation namespace, the
// same way the settings pane's card container does.
constexpr wchar_t kXmlns[] = LR"( xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" )";

constexpr int setupWidth = 760;
constexpr int setupHeight = 560;
constexpr int shortcutPage = 6;
constexpr int readyPage = 8;
// Welcome, Transcription and Microphone are the pages that carry a gate; every
// page after them is ungated, so a gate sweep stops here.
constexpr int lastGatedPage = 2;
// The width a provider row states for itself inside RadioButtons, which lays
// an item out to its content rather than to the list.
constexpr double choiceRowWidth = 560;

// Where a status sits on the mockup's scale: neutral while a probe runs or
// when a row is merely reporting, positive once a prerequisite holds, caution
// when it does not.
enum class StatusTone { Neutral, Positive, Caution };

TextBlock textBlock(const QString &value, bool wrap = true)
{
    TextBlock text;
    text.Text(hstring(value.toStdWString()));
    if (wrap) {
        text.TextWrapping(TextWrapping::Wrap);
    }
    return text;
}

TextBlock strongTextBlock(const QString &value)
{
    TextBlock text = textBlock(value, false);
    text.Style(Application::Current().Resources()
                   .Lookup(box_value(L"BodyStrongTextBlockStyle"))
                   .as<Style>());
    return text;
}

TextBlock secondaryTextBlock(const QString &value)
{
    TextBlock text = textBlock(value);
    text.FontSize(12);
    text.Opacity(0.72);
    return text;
}

// The brush a tone paints with: the theme's success and caution roles, and the
// secondary foreground for a status that is only reporting, such as "Not
// found" or "No cleanup". Red is never used — nothing the wizard reports is
// beyond the user's reach.
Brush toneBrush(StatusTone tone)
{
    const wchar_t *key = L"TextFillColorSecondaryBrush";
    if (tone == StatusTone::Positive) {
        key = L"SystemFillColorSuccessBrush";
    } else if (tone == StatusTone::Caution) {
        key = L"SystemFillColorCautionBrush";
    }
    const auto resources = Application::Current().Resources();
    const auto boxed = box_value(hstring(key));
    return resources.HasKey(boxed) ? resources.Lookup(boxed).as<Brush>() : nullptr;
}

// The Segoe Fluent glyph a tone leads with: a check, a warning triangle, or
// the dash the mockup's "Not found" and "No cleanup" rows carry.
wchar_t toneGlyph(StatusTone tone)
{
    switch (tone) {
    case StatusTone::Positive:
        return L'\uE73E';
    case StatusTone::Caution:
        return L'\uE7BA';
    case StatusTone::Neutral:
        break;
    }
    return L'\uE738';
}

FontIcon toneIcon(StatusTone tone)
{
    FontIcon icon;
    const wchar_t glyph = toneGlyph(tone);
    icon.Glyph(hstring(std::wstring_view(&glyph, 1)));
    icon.FontSize(14);
    icon.VerticalAlignment(VerticalAlignment::Center);
    if (const Brush brush = toneBrush(tone)) {
        icon.Foreground(brush);
    }
    return icon;
}

// One status: its glyph and its text, rewritten together every time a probe
// lands, so the colour and the mark never disagree with the words.
struct StatusCell {
    StackPanel root{nullptr};
    FontIcon glyph{nullptr};
    TextBlock text{nullptr};

    void set(const QString &value, StatusTone tone) const
    {
        const wchar_t mark = toneGlyph(tone);
        glyph.Glyph(hstring(std::wstring_view(&mark, 1)));
        text.Text(hstring(value.toStdWString()));
        const Brush brush = toneBrush(tone);
        if (!brush) {
            return;
        }
        glyph.Foreground(brush);
        text.Foreground(brush);
    }
};

StatusCell statusCell(const QString &value, StatusTone tone)
{
    StatusCell cell;
    cell.root = StackPanel();
    cell.root.Orientation(Orientation::Horizontal);
    cell.root.Spacing(6);
    cell.root.VerticalAlignment(VerticalAlignment::Center);
    cell.glyph = toneIcon(tone);
    cell.text = textBlock(QString(), false);
    cell.text.VerticalAlignment(VerticalAlignment::Center);
    cell.root.Children().Append(cell.glyph);
    cell.root.Children().Append(cell.text);
    cell.set(value, tone);
    return cell;
}

// The inset rule between rows of one card, drawn with the card's own stroke so
// it matches in every theme.
Border rowSeparator()
{
    static const hstring xaml = hstring(L"<Border") + kXmlns
        + LR"(BorderThickness="0,1,0,0" BorderBrush="{ThemeResource SettingsCardBorderBrush}"/>)";
    return XamlReader::Load(xaml).as<Border>();
}

// The brand marks, from assets/brand/, as XAML path geometry. The Windows
// front end has no packaged-asset step — its runtime files are copied next to
// the executable — and a Path is what this front end already parses at
// runtime, so the geometry travels in the binary rather than as a file that
// can go missing. The marks keep their own colours, which is what carries the
// recognition at 20px.
FrameworkElement brandMark(const QString &providerId)
{
    static const hstring claudeXaml = hstring(L"<Viewbox") + kXmlns
        + LR"(Width="20" Height="20"><Path Fill="#D97757" Data="F1 m4.7144 15.9555 4.7174-2.6471.079-.2307-.079-.1275h-.2307l-.7893-.0486-2.6956-.0729-2.3375-.0971-2.2646-.1214-.5707-.1215-.5343-.7042.0546-.3522.4797-.3218.686.0608 1.5179.1032 2.2767.1578 1.6514.0972 2.4468.255h.3886l.0546-.1579-.1336-.0971-.1032-.0972L6.973 9.8356l-2.55-1.6879-1.3356-.9714-.7225-.4918-.3643-.4614-.1578-1.0078.6557-.7225.8803.0607.2246.0607.8925.686 1.9064 1.4754 2.4893 1.8336.3643.3035.1457-.1032.0182-.0728-.164-.2733-1.3539-2.4467-1.445-2.4893-.6435-1.032-.17-.6194c-.0607-.255-.1032-.4674-.1032-.7285L6.287.1335 6.6997 0l.9957.1336.419.3642.6192 1.4147 1.0018 2.2282 1.5543 3.0296.4553.8985.2429.8318.091.255h.1579v-.1457l.1275-1.706.2368-2.0947.2307-2.6957.0789-.7589.3764-.9107.7468-.4918.5828.2793.4797.686-.0668.4433-.2853 1.8517-.5586 2.9021-.3643 1.9429h.2125l.2429-.2429.9835-1.3053 1.6514-2.0643.7286-.8196.85-.9046.5464-.4311h1.0321l.759 1.1293-.34 1.1657-1.0625 1.3478-.8804 1.1414-1.2628 1.7-.7893 1.36.0729.1093.1882-.0183 2.8535-.607 1.5421-.2794 1.8396-.3157.8318.3886.091.3946-.3278.8075-1.967.4857-2.3072.4614-3.4364.8136-.0425.0304.0486.0607 1.5482.1457.6618.0364h1.621l3.0175.2247.7892.522.4736.6376-.079.4857-1.2142.6193-1.6393-.3886-3.825-.9107-1.3113-.3279h-.1822v.1093l1.0929 1.0686 2.0035 1.8092 2.5075 2.3314.1275.5768-.3218.4554-.34-.0486-2.2039-1.6575-.85-.7468-1.9246-1.621h-.1275v.17l.4432.6496 2.3436 3.5214.1214 1.0807-.17.3521-.6071.2125-.6679-.1214-1.3721-1.9246L14.38 17.959l-1.1414-1.9428-.1397.079-.674 7.2552-.3156.3703-.7286.2793-.6071-.4614-.3218-.7468.3218-1.4753.3886-1.9246.3157-1.53.2853-1.9004.17-.6314-.0121-.0425-.1397.0182-1.4328 1.9672-2.1796 2.9446-1.7243 1.8456-.4128.164-.7164-.3704.0667-.6618.4008-.5889 2.386-3.0357 1.4389-1.882.929-1.0868-.0062-.1579h-.0546l-6.3385 4.1164-1.1293.1457-.4857-.4554.0608-.7467.2307-.2429 1.9064-1.3114Z"/></Viewbox>)";
    // The knot is one path stamped six times around the tile's centre, the way
    // chatgpt.svg draws it with <use>.
    static const hstring chatGptXaml = [] {
        std::wstring xaml = std::wstring(L"<Border") + kXmlns
            + LR"(Width="20" Height="20" CornerRadius="5" Background="#74AA9C">)"
            + LR"(<Viewbox Width="20" Height="20"><Canvas Width="2406" Height="2406">)";
        for (int angle = 0; angle < 360; angle += 60) {
            xaml += LR"(<Path Fill="White" Data="F1 M1107.3 299.1c-197.999 0-373.9 127.3-435.2 315.3L650 743.5v427.9c0 21.4 11 40.4 29.4 51.4l344.5 198.515V833.3h.1v-27.9L1372.7 604c33.715-19.52 70.44-32.857 108.47-39.828L1447.6 450.3C1361 353.5 1237.1 298.5 1107.3 299.1zm0 117.5-.6.6c79.699 0 156.3 27.5 217.6 78.4-2.5 1.2-7.4 4.3-11 6.1L952.8 709.3c-18.4 10.4-29.4 30-29.4 51.4V1248l-155.1-89.4V755.8c-.1-187.099 151.601-338.9 339-339.2z"><Path.RenderTransform>)"
                    LR"(<RotateTransform CenterX="1203" CenterY="1203" Angle=")"
                + std::to_wstring(angle) + LR"("/></Path.RenderTransform></Path>)";
        }
        xaml += LR"(</Canvas></Viewbox></Border>)";
        return hstring(xaml);
    }();

    if (providerId == QStringLiteral("claude") || providerId == QStringLiteral("anthropic")) {
        return XamlReader::Load(claudeXaml).as<FrameworkElement>();
    }
    if (providerId == QStringLiteral("codex") || providerId == QStringLiteral("openai")) {
        return XamlReader::Load(chatGptXaml).as<FrameworkElement>();
    }
    return nullptr;
}

// A 20px leading mark for a row that has no brand behind it.
FontIcon glyphMark(wchar_t glyph)
{
    FontIcon icon;
    icon.Glyph(hstring(std::wstring_view(&glyph, 1)));
    icon.FontSize(18);
    icon.VerticalAlignment(VerticalAlignment::Center);
    return icon;
}

// One card row: the leading mark, the text column, and the trailing status or
// button. The text column is the caller's, so a row can carry a note or a
// hint under its label.
Grid cardRow(const FrameworkElement &mark, const StackPanel &text, const FrameworkElement &trailing)
{
    Grid row;
    row.ColumnSpacing(12);
    row.Padding({0, 10, 0, 10});
    ColumnDefinition markColumn;
    markColumn.Width({0, GridUnitType::Auto});
    ColumnDefinition textColumn;
    textColumn.Width({1, GridUnitType::Star});
    ColumnDefinition trailingColumn;
    trailingColumn.Width({0, GridUnitType::Auto});
    row.ColumnDefinitions().Append(markColumn);
    row.ColumnDefinitions().Append(textColumn);
    row.ColumnDefinitions().Append(trailingColumn);
    if (mark) {
        mark.VerticalAlignment(VerticalAlignment::Center);
        row.Children().Append(mark);
    }
    text.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(text, 1);
    row.Children().Append(text);
    if (trailing) {
        Grid::SetColumn(trailing, 2);
        row.Children().Append(trailing);
    }
    return row;
}

// Appends a row to a list of rows, separated from the one above it.
void appendRow(const StackPanel &list, const Grid &row)
{
    if (list.Children().Size()) {
        list.Children().Append(rowSeparator());
    }
    list.Children().Append(row);
}

// The text column of a card row: a label, and whatever the caller adds under
// it.
StackPanel rowText(const TextBlock &label)
{
    StackPanel text;
    text.Spacing(2);
    text.Children().Append(label);
    return text;
}

// The mockup's card: the settings pane's card container, with an optional bold
// title, appended to the page column. Rows go into the returned body.
StackPanel card(const StackPanel &column, const QString &title)
{
    StackPanel body;
    body.Spacing(8);
    body.Margin({16, 14, 16, 14});
    if (!title.isEmpty()) {
        body.Children().Append(strongTextBlock(title));
    }
    column.Children().Append(win::cardContainer(body));
    return body;
}

// A list of rows inside a card, flush with the card's padding.
StackPanel rowList(const StackPanel &body)
{
    StackPanel list;
    body.Children().Append(list);
    return list;
}

ComboBox combo(const QList<QPair<QString, QString>> &options, const QString &selected)
{
    ComboBox control;
    control.MinWidth(240);
    int selectedIndex = 0;
    for (int index = 0; index < options.size(); ++index) {
        control.Items().Append(box_value(hstring(options.at(index).second.toStdWString())));
        if (options.at(index).first == selected) {
            selectedIndex = index;
        }
    }
    control.SelectedIndex(selectedIndex);
    return control;
}

// A checkbox whose label wraps. A CheckBox's string content is laid out on one
// line, and these sentences are wider than the wizard.
CheckBox wrappingCheckBox(const QString &label)
{
    CheckBox box;
    box.Content(textBlock(label));
    return box;
}

StackPanel settingRow(const QString &label, const Control &control)
{
    StackPanel row;
    row.Spacing(6);
    row.Children().Append(strongTextBlock(label));
    row.Children().Append(control);
    return row;
}

// A provider's display name, by id, or the id itself when the registry has no
// such provider.
QString providerLabel(const QList<ProviderDescriptor> &providers, const QString &id)
{
    for (const ProviderDescriptor &provider : providers) {
        if (provider.id == id) {
            return provider.label;
        }
    }
    return id;
}

void showProviderStats(const StackPanel &panel, const QList<ProviderDescriptor> &providers,
                       const QString &id)
{
    panel.Children().Clear();
    for (const ProviderDescriptor &provider : providers) {
        if (provider.id != id) {
            continue;
        }
        // The mockup's ruled two-column table: the label in the secondary
        // foreground on the left, the value beside it.
        for (const ProviderStat &stat : provider.stats) {
            Grid row;
            row.ColumnSpacing(16);
            row.Padding({0, 6, 0, 6});
            ColumnDefinition labelColumn;
            labelColumn.Width({150, GridUnitType::Pixel});
            ColumnDefinition valueColumn;
            valueColumn.Width({1, GridUnitType::Star});
            row.ColumnDefinitions().Append(labelColumn);
            row.ColumnDefinitions().Append(valueColumn);
            row.Children().Append(secondaryTextBlock(stat.label));
            TextBlock value = textBlock(stat.value);
            value.FontSize(12);
            Grid::SetColumn(value, 1);
            row.Children().Append(value);
            appendRow(panel, row);
        }
        break;
    }
    panel.Visibility(panel.Children().Size() ? Visibility::Visible : Visibility::Collapsed);
}

QList<QPair<QString, QString>> profileOptions()
{
    return {{QStringLiteral("work"), QStringLiteral("Work")},
            {QStringLiteral("email"), QStringLiteral("Email")},
            {QStringLiteral("personal"), QStringLiteral("Personal")},
            {QStringLiteral("other"), QStringLiteral("Other")},
            {QStringLiteral("ai_coding"), QStringLiteral("AI coding")}};
}

QStringList welcomeCopy()
{
    return {QStringLiteral("Speecher records a short dictation, turns it into text, and sends it to the app you were using."),
            QStringLiteral("This assistant checks everything dictation needs: your speech service, microphone, and how text reaches your apps.")};
}

// The Ready page has to describe the mode that was actually chosen: a
// push-to-talk user told to "press it again to stop" is told a falsehood.
QString readyInstruction(ShortcutActivationMode mode, const QString &display)
{
    switch (mode) {
    case ShortcutActivationMode::PushToTalk:
        return QStringLiteral("To dictate, hold %1 while you speak.").arg(display);
    case ShortcutActivationMode::Hybrid:
        return QStringLiteral("To dictate, tap %1 to toggle, or hold it to dictate until release.")
            .arg(display);
    case ShortcutActivationMode::Toggle:
        break;
    }
    return QStringLiteral("To dictate, press %1 to start, press it again to stop.").arg(display);
}

} // namespace

struct SetupWindow::Native {
    Native(ApplicationController *owner,
           std::function<void()> reportFirstFrame,
           SetupWindow *q)
        : controller(owner)
        , firstFrame(std::move(reportFirstFrame))
        , setup(q)
        , launchAtLogin(controller->settings()->launchAtLogin())
    {
        microphone = controller->platform()->createAudioInput(controller->settings(), q);
        QObject::connect(microphone, &AudioInput::levelChanged, q, [this](float value) {
            if (microphoneLevel) {
                microphoneLevel.Value(std::clamp(value, 0.0f, 1.0f));
            }
            if (value <= 0.01f) {
                return;
            }
            if (microphoneStatus) {
                microphoneStatus.Text(L"Microphone input detected.");
            }
            if (!microphoneDetected) {
                microphoneDetected = true;
                refreshGates();
            }
        });
        QObject::connect(microphone, &AudioInput::failed, q, [this](const QString &message) {
            if (microphoneStatus) {
                microphoneStatus.Text(hstring(message.toStdWString()));
            }
            if (microphoneProblem) {
                microphoneProblem.IsOpen(true);
            }
            // The capture that satisfied the gate has died; Continue must not
            // stay open on a microphone that is no longer listening.
            if (microphoneDetected) {
                microphoneDetected = false;
                refreshGates();
            }
        });
    }

    ~Native()
    {
        microphone->stop();
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
        window.Closed([this](const auto &, const auto &) {
            microphone->stop();
            resumeShortcut();
            window = nullptr;
            content = nullptr;
        });

        Grid root;
        root.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        RowDefinition titleRow;
        titleRow.Height({48, GridUnitType::Pixel});
        RowDefinition contentRow;
        contentRow.Height({1, GridUnitType::Star});
        RowDefinition barRow;
        barRow.Height({72, GridUnitType::Pixel});
        root.RowDefinitions().Append(titleRow);
        root.RowDefinitions().Append(contentRow);
        root.RowDefinitions().Append(barRow);

        TitleBar titleBar;
        titleBar.Title(L"Speecher Setup");
        titleBar.IsBackButtonVisible(false);
        Grid::SetRow(titleBar, 0);
        root.Children().Append(titleBar);

        ScrollViewer scroll;
        scroll.Padding({48, 28, 48, 24});
        content = StackPanel();
        scroll.Content(content);
        Grid::SetRow(scroll, 1);
        root.Children().Append(scroll);

        Grid bottom;
        bottom.Padding({48, 12, 48, 12});
        ColumnDefinition left;
        left.Width({1, GridUnitType::Star});
        ColumnDefinition right;
        right.Width({1, GridUnitType::Auto});
        bottom.ColumnDefinitions().Append(left);
        bottom.ColumnDefinitions().Append(right);

        skip = Button();
        skip.Content(box_value(L"Skip setup"));
        skip.VerticalAlignment(VerticalAlignment::Center);
        skip.Click([this](const auto &, const auto &) { complete(true); });
        bottom.Children().Append(skip);

        StackPanel navigation;
        navigation.Orientation(Orientation::Horizontal);
        navigation.Spacing(8);
        back = Button();
        back.Content(box_value(L"Back"));
        back.Click([this](const auto &, const auto &) { showPage(pageIndex - 1); });
        next = Button();
        next.Style(Application::Current().Resources()
                       .Lookup(box_value(L"AccentButtonStyle"))
                       .as<Style>());
        next.Click([this](const auto &, const auto &) {
            if (singlePage) {
                window.Close();
            } else if (pageIndex == SetupWindow::pageTitles().size() - 1) {
                complete(false);
            } else {
                showPage(pageIndex + 1);
            }
        });
        navigation.Children().Append(back);
        navigation.Children().Append(next);
        Grid::SetColumn(navigation, 1);
        bottom.Children().Append(navigation);
        Grid::SetRow(bottom, 2);
        root.Children().Append(bottom);

        root.Loaded([this](const auto &, const auto &) {
            QTimer::singleShot(0, setup, firstFrame);
        });
        window.Content(root);
        window.SetTitleBar(titleBar);

        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        SetWindowTextW(handle, L"Speecher Setup");
        POINT pointer{};
        GetCursorPos(&pointer);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), &monitor);
        // Land on the cursor's monitor first so the window's DPI is that
        // monitor's; setupWidth/Height are DIPs and SetWindowPos wants
        // physical pixels.
        SetWindowPos(handle, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        const double scale = GetDpiForWindow(handle) / 96.0;
        const int width = int(setupWidth * scale + 0.5);
        const int height = int(setupHeight * scale + 0.5);
        const int x = monitor.rcWork.left
            + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
        const int y = monitor.rcWork.top
            + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
        SetWindowPos(handle, nullptr, x, y, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void show(SetupAssistantPage requested)
    {
        ensureWindow();
        singlePage = requested == SetupAssistantPage::GlobalShortcut;
        showPage(singlePage ? shortcutPage : 0);
        window.Activate();
        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        AllowSetForegroundWindow(ASFW_ANY);
        SetForegroundWindow(handle);
    }

    // A page's prerequisite, as the wizard currently knows it. Every page past
    // lastGatedPage asks nothing of the user that can fail.
    bool gateSatisfied(int index) const
    {
        switch (index) {
        case 0:
            // Welcome: at least one provider sign-in is on this machine.
            for (auto entry = speechReady.cbegin(); entry != speechReady.cend(); ++entry) {
                if (entry.value()) {
                    return true;
                }
            }
            return false;
        case 1:
            return speechReady.value(controller->settings()->speechProvider(), false);
        case 2:
            return microphoneDetected;
        default:
            return true;
        }
    }

    // The first page whose gate is unmet, or -1 while every gate holds.
    int firstUnsatisfiedPage() const
    {
        for (int index = 0; index <= lastGatedPage; ++index) {
            if (!gateSatisfied(index)) {
                return index;
            }
        }
        return -1;
    }

    // Next only moves on from a page whose prerequisite is met, and Skip is
    // only offered once nothing is left to meet — skipping past a gate leaves
    // an app that cannot dictate.
    void refreshGates()
    {
        if (!next || !skip) {
            return;
        }
        // The last page's button finishes, so it answers for every gate rather
        // than its own: a prerequisite that lapsed behind the walk must hold
        // Finish shut, not bounce the person back after a click.
        const bool ready = pageIndex == readyPage ? firstUnsatisfiedPage() < 0
                                                  : gateSatisfied(pageIndex);
        next.IsEnabled(singlePage || ready);
        const bool offerSkip = !singlePage && pageIndex != readyPage && firstUnsatisfiedPage() < 0;
        skip.Visibility(offerSkip ? Visibility::Visible : Visibility::Collapsed);
    }

    // A speech provider's credential probe, off the UI thread where the
    // provider offers a job, reported back on it. Results from a superseded
    // check or a page the wizard has since left are discarded.
    void probeSpeechProvider(const QString &id,
                             quint64 generation,
                             std::function<void(const SpeechPrepareResult &)> report)
    {
        SpeechTranscriber *transcriber = controller->providerRegistry()->speechProvider(id);
        if (!transcriber) {
            report({false, QStringLiteral("No transcription service is available.")});
            return;
        }
        const SpeechSettings settings = controller->settings()->snapshot().speech;
        std::optional<SpeechPrepareJob> job = transcriber->createPrepareJob(settings);
        if (!job || !job->run) {
            report(transcriber->prepare(settings));
            return;
        }
        auto result = std::make_shared<SpeechPrepareResult>();
        auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
        QThread *thread = QThread::create([prepareJob, result] {
            *result = prepareJob->run();
        });
        QObject::connect(thread, &QThread::finished, setup,
                         [this, generation, prepareJob, result, report] {
            if (generation != checkGeneration) {
                return;
            }
            if (prepareJob->apply) {
                prepareJob->apply(*result);
            }
            report(*result);
        });
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }

    // The refinement equivalent: the refiner's own credential resolution,
    // which is all "ready" can mean before a dictation happens.
    void probeRefinementProvider(const QString &id,
                                 quint64 generation,
                                 std::function<void(bool)> report)
    {
        TranscriptRefiner *refiner = controller->providerRegistry()->refinementProvider(id);
        if (!refiner) {
            report(false);
            return;
        }
        const RefinementSettings settings = controller->settings()->snapshot().refinement;
        std::optional<RefinementRefreshJob> job = refiner->createRefreshJob(settings);
        if (!job || !job->run) {
            report(refiner->prepare(settings).ok);
            return;
        }
        auto result = std::make_shared<RefinementRefreshResult>();
        auto refreshJob = std::make_shared<RefinementRefreshJob>(std::move(*job));
        QThread *thread = QThread::create([refreshJob, result] {
            *result = refreshJob->run();
        });
        QObject::connect(thread, &QThread::finished, setup,
                         [this, generation, refreshJob, result, report] {
            if (generation != checkGeneration) {
                return;
            }
            if (refreshJob->apply) {
                refreshJob->apply(*result);
            }
            report(result->ok);
        });
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }

    void showPage(int index)
    {
        if (index < 0 || index >= SetupWindow::pageTitles().size()) {
            return;
        }
        microphone->stop();
        microphoneLevel = nullptr;
        microphoneStatus = nullptr;
        microphoneProblem = nullptr;
        shortcutStatus = nullptr;
        readyBody = nullptr;
        ++checkGeneration;
        pageIndex = index;
        // The recorder page needs the bound chord delivered as a key event,
        // which RegisterHotKey would otherwise consume system-wide.
        if (index == shortcutPage) {
            suspendShortcut();
        } else {
            resumeShortcut();
        }
        content.Children().Clear();
        switch (index) {
        case 0: showWelcome(); break;
        case 1: showTranscription(); break;
        case 2: showMicrophone(); break;
        case 3: showDelivery(); break;
        case 4: showRefinement(); break;
        case 5: showProfiles(); break;
        case 6: showShortcut(); break;
        case 7: showStartAtLogin(); break;
        case 8: showReady(); break;
        }
        back.Visibility(singlePage || index == 0 ? Visibility::Collapsed : Visibility::Visible);
        next.Content(box_value(singlePage ? L"Done"
                                          : (index == readyPage ? L"Finish" : L"Next")));
        refreshGates();
    }

    // The mockup's page frame: the title with "Step N of 9" grey on the right,
    // then the page's lead paragraph. The single-page shortcut recorder is not
    // a step in a walk, so it carries no counter.
    StackPanel page(const QString &title, const QString &body)
    {
        StackPanel column;
        column.Spacing(12);
        column.MaxWidth(660);
        column.HorizontalAlignment(HorizontalAlignment::Stretch);

        Grid header;
        header.ColumnSpacing(12);
        ColumnDefinition titleColumn;
        titleColumn.Width({1, GridUnitType::Star});
        ColumnDefinition stepColumn;
        stepColumn.Width({0, GridUnitType::Auto});
        header.ColumnDefinitions().Append(titleColumn);
        header.ColumnDefinitions().Append(stepColumn);
        TextBlock heading = textBlock(title);
        heading.Style(Application::Current().Resources()
                          .Lookup(box_value(L"TitleTextBlockStyle"))
                          .as<Style>());
        header.Children().Append(heading);
        if (!singlePage) {
            TextBlock step = secondaryTextBlock(
                QStringLiteral("Step %1 of %2")
                    .arg(pageIndex + 1)
                    .arg(SetupWindow::pageTitles().size()));
            step.VerticalAlignment(VerticalAlignment::Bottom);
            Grid::SetColumn(step, 1);
            header.Children().Append(step);
        }
        column.Children().Append(header);
        if (!body.isEmpty()) {
            TextBlock description = textBlock(body);
            description.Opacity(0.72);
            column.Children().Append(description);
        }
        return column;
    }

    void showWelcome()
    {
        const QStringList copy = welcomeCopy();
        StackPanel panel = page(
            QStringLiteral("Welcome to Speecher"),
            copy.at(0));
        panel.Children().Append(textBlock(copy.at(1)));

        StackPanel before = card(panel, QStringLiteral("Before you start"));
        before.Children().Append(secondaryTextBlock(QStringLiteral(
            "Speecher uses your existing ChatGPT or Claude sign-in. Install and sign in to one "
            "of these, then choose Check again:")));
        StackPanel list = rowList(before);

        QStringList ids;
        std::vector<StatusCell> statuses;
        std::vector<TextBlock> hints;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->speechProviders()) {
            TextBlock hint = secondaryTextBlock(provider.setupHint);
            hint.Visibility(Visibility::Collapsed);
            StackPanel text = rowText(
                textBlock(credentialSourceLabel(provider.id, provider.label)));
            text.Children().Append(hint);
            const StatusCell status = statusCell(QStringLiteral("Checking…"), StatusTone::Neutral);
            appendRow(list, cardRow(brandMark(provider.id), text, status.root));
            ids.append(provider.id);
            statuses.push_back(status);
            hints.push_back(hint);
        }

        Button check;
        check.Content(box_value(L"Check again"));
        check.HorizontalAlignment(HorizontalAlignment::Left);
        const auto runChecks = [this, ids, statuses, hints] {
            const quint64 generation = ++checkGeneration;
            for (int index = 0; index < ids.size(); ++index) {
                const StatusCell status = statuses.at(size_t(index));
                const TextBlock hint = hints.at(size_t(index));
                status.set(QStringLiteral("Checking…"), StatusTone::Neutral);
                probeSpeechProvider(ids.at(index), generation,
                                    [this, id = ids.at(index), status, hint](
                                        const SpeechPrepareResult &result) {
                    speechReady.insert(id, result.ok);
                    status.set(result.ok ? QStringLiteral("Sign-in found")
                                         : QStringLiteral("Not found"),
                               result.ok ? StatusTone::Positive : StatusTone::Neutral);
                    hint.Visibility(result.ok ? Visibility::Collapsed : Visibility::Visible);
                    refreshGates();
                });
            }
        };
        check.Click([runChecks](const auto &, const auto &) { runChecks(); });
        before.Children().Append(check);
        content.Children().Append(panel);
        runChecks();
    }

    // Selects a row on the wizard's own behalf, remembering the index so the
    // SelectionChanged that follows is not mistaken for the user's choice.
    void selectProgrammatically(const RadioButtons &choices, int &pending, int index)
    {
        pending = index;
        choices.SelectedIndex(index);
    }

    // True when this event is the echo of our own write, which it then forgets.
    bool wasProgrammatic(int &pending, int index)
    {
        if (pending != index) {
            return false;
        }
        pending = -1;
        return true;
    }

    // Re-asserts the selection once the control is really loaded: a
    // SelectedIndex written before the item repeater existed can render as no
    // selection at all.
    void reselectOnLoad(const RadioButtons &choices,
                        const QList<QPair<QString, QString>> &options,
                        int &pending,
                        std::function<QString()> currentId)
    {
        choices.Loaded([this, choices, options, &pending, currentId](const auto &, const auto &) {
            const QString wanted = currentId();
            for (int index = 0; index < options.size(); ++index) {
                if (options.at(index).first != wanted) {
                    continue;
                }
                if (choices.SelectedIndex() != index) {
                    selectProgrammatically(choices, pending, index);
                }
                return;
            }
        });
    }

    // Once per wizard run, and never over a choice made here: a saved provider
    // whose probe failed gives way to one whose probe succeeded.
    void autoSelectSpeechProvider(const RadioButtons &choices,
                                  const QList<QPair<QString, QString>> &options)
    {
        if (speechSelectionSettled) {
            return;
        }
        const QString saved = controller->settings()->speechProvider();
        if (!speechReady.contains(saved) || speechReady.value(saved)) {
            return;
        }
        for (int index = 0; index < options.size(); ++index) {
            if (!speechReady.value(options.at(index).first, false)) {
                continue;
            }
            // Persisted here rather than left to the selection handler, so the
            // switch holds whether or not the control reports it.
            speechSelectionSettled = true;
            controller->settings()->setSpeechProvider(options.at(index).first);
            selectProgrammatically(choices, programmaticSpeechIndex, index);
            return;
        }
    }

    void showTranscription()
    {
        StackPanel panel = page(
            QStringLiteral("Transcription"),
            QStringLiteral("Choose the service Speecher uses to turn speech into a raw transcript."));
        QList<QPair<QString, QString>> options;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->speechProviders()) {
            options.append({provider.id, provider.label});
        }
        // Every service is on screen with its own readiness, rather than one
        // hidden behind a dropdown.
        RadioButtons choices;
        std::vector<StatusCell> statuses;
        int selectedIndex = 0;
        for (int index = 0; index < options.size(); ++index) {
            const QString id = options.at(index).first;
            const StatusCell status = statusCell(QStringLiteral("Checking…"), StatusTone::Neutral);
            Grid item = cardRow(brandMark(id), rowText(strongTextBlock(options.at(index).second)),
                                status.root);
            // A RadioButtons item is laid out to its content's width, so the
            // row states the width the mockup's card has; without it the
            // status would sit against the name rather than at the right edge.
            item.MinWidth(choiceRowWidth);
            choices.Items().Append(item);
            statuses.push_back(status);
            if (id == controller->settings()->speechProvider()) {
                selectedIndex = index;
            }
        }
        // Selected before the handler exists: restoring the saved choice must
        // not look like the user making one, nor re-persist it.
        selectProgrammatically(choices, programmaticSpeechIndex, selectedIndex);
        reselectOnLoad(choices, options, programmaticSpeechIndex,
                       [this] { return controller->settings()->speechProvider(); });

        // Codex only; describeSelected() below decides when it is on screen.
        CheckBox accuracy = wrappingCheckBox(
            QStringLiteral("Extra transcription accuracy (will increase transcription time)"));
        accuracy.IsChecked(controller->settings()->codexFinalRetranscribe());
        // Settled here too: the probes are async, so describeSelected() first
        // runs a beat later and the row would flash on for a non-Codex choice.
        accuracy.Visibility(controller->settings()->speechProvider() == QStringLiteral("codex")
                                ? Visibility::Visible : Visibility::Collapsed);
        StackPanel stats;
        const StatusCell status = statusCell(QString(), StatusTone::Neutral);
        status.root.VerticalAlignment(VerticalAlignment::Top);
        TextBlock hint = secondaryTextBlock(QString());
        Button check;
        check.Content(box_value(L"Check again"));
        check.VerticalAlignment(VerticalAlignment::Top);

        // The credential hint and Check again belong to a service that is not
        // signed in; a ready one needs neither.
        const auto describeSelected = [this, choices, options, stats, status, hint, check,
                                       accuracy] {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                status.set(QStringLiteral("No transcription service is available."),
                           StatusTone::Caution);
                return;
            }
            const QString id = options.at(index).first;
            showProviderStats(stats, controller->providerRegistry()->speechProviders(), id);
            accuracy.Visibility(id == QStringLiteral("codex")
                                    ? Visibility::Visible : Visibility::Collapsed);
            const bool ready = speechReady.value(id, false);
            const bool checked = speechReady.contains(id);
            status.set(speechMessage.value(id, QStringLiteral("Checking…")),
                       !checked ? StatusTone::Neutral
                                : (ready ? StatusTone::Positive : StatusTone::Caution));
            QString credential;
            for (const ProviderDescriptor &provider :
                 controller->providerRegistry()->speechProviders()) {
                if (provider.id == id) {
                    credential = provider.setupHint;
                    break;
                }
            }
            hint.Text(hstring(credential.toStdWString()));
            const Visibility unready = checked && !ready ? Visibility::Visible
                                                         : Visibility::Collapsed;
            hint.Visibility(unready);
            check.Visibility(unready);
        };
        choices.SelectionChanged([this, choices, options, describeSelected](const auto &,
                                                                           const auto &) {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            if (!wasProgrammatic(programmaticSpeechIndex, index)) {
                speechSelectionSettled = true;
            }
            controller->settings()->setSpeechProvider(options.at(index).first);
            describeSelected();
            refreshGates();
        });

        const auto runChecks = [this, choices, options, statuses, describeSelected] {
            const quint64 generation = ++checkGeneration;
            for (int index = 0; index < options.size(); ++index) {
                const QString id = options.at(index).first;
                const QString label = options.at(index).second;
                const StatusCell rowStatus = statuses.at(size_t(index));
                rowStatus.set(QStringLiteral("Checking…"), StatusTone::Neutral);
                probeSpeechProvider(id, generation,
                                    [this, id, label, rowStatus, choices, options,
                                     describeSelected](const SpeechPrepareResult &result) {
                    speechReady.insert(id, result.ok);
                    speechMessage.insert(id, result.ok
                                                 ? QStringLiteral("%1 is ready.").arg(label)
                                                 : result.message);
                    rowStatus.set(result.ok ? QStringLiteral("Ready")
                                            : QStringLiteral("Not set up"),
                                  result.ok ? StatusTone::Positive : StatusTone::Caution);
                    autoSelectSpeechProvider(choices, options);
                    describeSelected();
                    refreshGates();
                });
            }
        };
        check.Click([runChecks](const auto &, const auto &) { runChecks(); });
        accuracy.Click([this, accuracy](const auto &, const auto &) {
            controller->settings()->setCodexFinalRetranscribe(accuracy.IsChecked().Value());
        });
        panel.Children().Append(choices);
        panel.Children().Append(stats);
        // Under the facts about the chosen service, matching the Qt page and
        // where the refinement step puts Fast mode.
        panel.Children().Append(accuracy);
        StackPanel message;
        message.Spacing(4);
        message.Children().Append(status.root);
        message.Children().Append(hint);
        panel.Children().Append(cardRow(FrameworkElement{nullptr}, message, check));
        content.Children().Append(panel);
        runChecks();
    }

    void showMicrophone()
    {
        StackPanel panel = page(
            QStringLiteral("Microphone"),
            QStringLiteral("Choose the input Speecher should record. Speak normally; setup continues once the level moves."));
        const QList<AudioInputDeviceInfo> devices = controller->platform()->availableAudioInputDevices();
        QList<QPair<QString, QString>> options{{QString(), QStringLiteral("System default")}};
        for (const AudioInputDeviceInfo &device : devices) {
            options.append({device.id, device.label});
        }
        ComboBox device = combo(options, controller->settings()->audioInputDeviceId());
        device.SelectionChanged([this, device, options](const auto &, const auto &) {
            if (device.SelectedIndex() >= 0) {
                controller->settings()->setAudioInputDeviceId(options.at(device.SelectedIndex()).first);
                startMicrophone();
            }
        });
        microphoneLevel = ProgressBar();
        microphoneLevel.Minimum(0);
        microphoneLevel.Maximum(1);
        microphoneStatus = textBlock(QStringLiteral("Listening for microphone input…"));
        microphoneProblem = InfoBar();
        microphoneProblem.Title(L"Check microphone privacy");
        microphoneProblem.Message(L"Allow desktop apps to use the microphone, then check again.");
        microphoneProblem.Severity(InfoBarSeverity::Warning);
        microphoneProblem.IsClosable(true);
        microphoneProblem.IsOpen(false);
        Button openSettings;
        openSettings.Content(box_value(L"Open Microphone settings"));
        openSettings.Click([](const auto &, const auto &) {
            ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-microphone",
                          nullptr, nullptr, SW_SHOWNORMAL);
        });
        microphoneProblem.ActionButton(openSettings);
        panel.Children().Append(settingRow(QStringLiteral("Input device"), device));
        panel.Children().Append(settingRow(QStringLiteral("Live level"), microphoneLevel));
        panel.Children().Append(microphoneStatus);
        panel.Children().Append(microphoneProblem);
        content.Children().Append(panel);
        startMicrophone();
    }

    void startMicrophone()
    {
        microphone->stop();
        if (!microphoneLevel) {
            return;
        }
        microphoneLevel.Value(0);
        // A device that has not been heard from yet has not passed the gate;
        // only this meter moving reopens it.
        microphoneDetected = false;
        refreshGates();
        QString error;
        if (!microphone->start(&error)) {
            microphoneStatus.Text(hstring(error.toStdWString()));
            microphoneProblem.IsOpen(true);
        }
    }

    void showDelivery()
    {
        StackPanel panel = page(
            QStringLiteral("Text delivery"),
            QStringLiteral("Nothing to install. Speecher pastes with the Windows clipboard."));
        const QList<QPair<QString, QString>> formats{
            {QStringLiteral("plain"), QStringLiteral("Plain text")},
            {QStringLiteral("html"), QStringLiteral("HTML and plain text")}};
        ComboBox format = combo(formats, outputFormatName(controller->settings()->outputFormat()));
        format.SelectionChanged([this, format, formats](const auto &, const auto &) {
            controller->settings()->setOutputFormat(
                outputFormatFromString(formats.at(format.SelectedIndex()).first));
        });
        CheckBox restore = wrappingCheckBox(restoreClipboardDescription());
        restore.IsChecked(controller->settings()->restoreClipboardAfterTyping());
        restore.Click([this, restore](const auto &, const auto &) {
            controller->settings()->setRestoreClipboardAfterTyping(restore.IsChecked().Value());
        });
        // One card for the whole page, the way the mockup groups delivery:
        // the format row, then the clipboard sentence under the same stroke.
        StackPanel rows = rowList(card(panel, QString()));
        appendRow(rows, cardRow(glyphMark(L'\uE765'),
                                rowText(textBlock(QStringLiteral("Clipboard format"), false)),
                                format));
        rows.Children().Append(rowSeparator());
        restore.Margin({0, 12, 0, 0});
        rows.Children().Append(restore);
        content.Children().Append(panel);
    }

    // The refinement twin of autoSelectSpeechProvider. None is a deliberate
    // choice, not an unready provider, so a saved None is left alone.
    void autoSelectRefinementProvider(const RadioButtons &choices,
                                      const QList<QPair<QString, QString>> &options)
    {
        if (refinementSelectionSettled) {
            return;
        }
        const QString saved = controller->settings()->refinementProvider();
        if (saved == QStringLiteral("none") || !refinementReady.contains(saved)
            || refinementReady.value(saved)) {
            return;
        }
        for (int index = 0; index < options.size(); ++index) {
            if (!refinementReady.value(options.at(index).first, false)) {
                continue;
            }
            refinementSelectionSettled = true;
            controller->settings()->setRefinementProvider(options.at(index).first);
            selectProgrammatically(choices, programmaticRefinementIndex, index);
            return;
        }
    }

    void showRefinement()
    {
        StackPanel panel = page(
            QStringLiteral("Refinement"),
            QStringLiteral("Refinement can clean up a raw transcript after dictation. Choose a provider, or None to skip cleanup."));
        QList<QPair<QString, QString>> options;
        // Both refiners ride on a transcription sign-in the user already made.
        // The descriptor says which one; nothing else on the page does.
        QHash<QString, QString> credentialNotes;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->refinementProviders()) {
            options.append({provider.id, provider.label});
            credentialNotes.insert(provider.id, provider.setupHint);
        }
        options.append({QStringLiteral("none"), QStringLiteral("None")});
        RadioButtons choices;
        std::vector<StatusCell> statuses;
        int selectedIndex = 0;
        for (int index = 0; index < options.size(); ++index) {
            const QString id = options.at(index).first;
            const bool none = id == QStringLiteral("none");
            // None is a choice, not an unready provider: it reports what it
            // does rather than a readiness the wizard never probes for.
            const StatusCell status = statusCell(none ? QStringLiteral("No cleanup")
                                                      : QStringLiteral("Checking…"),
                                                 StatusTone::Neutral);
            StackPanel text = rowText(strongTextBlock(options.at(index).second));
            const QString note = credentialNotes.value(id);
            text.Children().Append(secondaryTextBlock(
                note.isEmpty() ? QStringLiteral("Skip cleanup entirely.") : note));
            FrameworkElement mark = brandMark(id);
            if (none) {
                mark = glyphMark(L'');
            }
            Grid item = cardRow(mark, text, status.root);
            item.MinWidth(choiceRowWidth);
            choices.Items().Append(item);
            statuses.push_back(status);
            if (id == controller->settings()->refinementProvider()) {
                selectedIndex = index;
            }
        }
        selectProgrammatically(choices, programmaticRefinementIndex, selectedIndex);
        reselectOnLoad(choices, options, programmaticRefinementIndex,
                       [this] { return controller->settings()->refinementProvider(); });

        StackPanel stats;
        // Directly under the cards, where the mockup puts it: the warning
        // belongs to the selection above it, not to the stats below.
        InfoBar warning;
        warning.Severity(InfoBarSeverity::Warning);
        warning.IsClosable(false);
        warning.IsOpen(false);
        CheckBox fast = wrappingCheckBox(QStringLiteral("Fast mode"));
        const auto refreshSelection = [this, choices, fast, options, stats, warning] {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            const QString id = options.at(index).first;
            showProviderStats(stats, controller->providerRegistry()->refinementProviders(), id);
            fast.Visibility(id == QStringLiteral("openai") || id == QStringLiteral("anthropic")
                                ? Visibility::Visible : Visibility::Collapsed);
            if (id == QStringLiteral("openai")) {
                fast.IsChecked(controller->settings()->openAiFastMode());
            } else if (id == QStringLiteral("anthropic")) {
                fast.IsChecked(controller->settings()->anthropicFastMode());
            }
            // Refinement stays ungated: an unready provider is a warning, not
            // a wall, because the raw transcript is still delivered.
            const bool unready = refinementReady.contains(id) && !refinementReady.value(id);
            warning.IsOpen(unready);
            if (unready) {
                warning.Message(hstring(
                    QStringLiteral("%1 is not signed in. Dictation will deliver the raw transcript.")
                        .arg(options.at(index).second)
                        .toStdWString()));
            }
        };
        choices.SelectionChanged([this, choices, options, refreshSelection](const auto &,
                                                                           const auto &) {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            if (!wasProgrammatic(programmaticRefinementIndex, index)) {
                refinementSelectionSettled = true;
            }
            controller->settings()->setRefinementProvider(options.at(index).first);
            refreshSelection();
        });
        fast.Click([this, choices, fast, options](const auto &, const auto &) {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            const bool checked = fast.IsChecked().Value();
            const QString id = options.at(index).first;
            if (id == QStringLiteral("openai")) {
                controller->settings()->setOpenAiFastMode(checked);
            } else if (id == QStringLiteral("anthropic")) {
                controller->settings()->setAnthropicFastMode(checked);
            }
        });
        panel.Children().Append(choices);
        panel.Children().Append(warning);
        panel.Children().Append(stats);
        panel.Children().Append(fast);
        content.Children().Append(panel);
        refreshSelection();

        const quint64 generation = ++checkGeneration;
        for (int index = 0; index < options.size(); ++index) {
            const QString id = options.at(index).first;
            if (id == QStringLiteral("none")) {
                continue;
            }
            const StatusCell rowStatus = statuses.at(size_t(index));
            probeRefinementProvider(id, generation,
                                    [this, id, rowStatus, choices, options,
                                     refreshSelection](bool ok) {
                refinementReady.insert(id, ok);
                rowStatus.set(ok ? QStringLiteral("Ready") : QStringLiteral("Not signed in"),
                              ok ? StatusTone::Positive : StatusTone::Caution);
                autoSelectRefinementProvider(choices, options);
                refreshSelection();
            });
        }
    }

    void showProfiles()
    {
        StackPanel panel = page(
            QStringLiteral("Writing profiles"),
            QStringLiteral("Speecher picks a writing profile from the app you dictate into. Choose the fallback profile and how much cleanup and tone adjustment each one gets."));
        const auto profiles = profileOptions();
        ComboBox fallback = combo(profiles, controller->settings()->defaultWritingProfile());
        fallback.SelectionChanged([this, fallback, profiles](const auto &, const auto &) {
            controller->settings()->setDefaultWritingProfile(profiles.at(fallback.SelectedIndex()).first);
        });
        panel.Children().Append(settingRow(QStringLiteral("Default profile"), fallback));

        const QList<QPair<QString, QString>> cleanup{
            {QStringLiteral("none"), QStringLiteral("None")},
            {QStringLiteral("light_cleanup"), QStringLiteral("Light")},
            {QStringLiteral("balanced"), QStringLiteral("Medium")},
            {QStringLiteral("strong_polish"), QStringLiteral("High")}};
        const QList<QPair<QString, QString>> tones{
            {QStringLiteral("none"), QStringLiteral("No tone override")},
            {QStringLiteral("formal"), QStringLiteral("Formal")},
            {QStringLiteral("casual"), QStringLiteral("Casual")},
            {QStringLiteral("very_casual"), QStringLiteral("Very casual")},
            {QStringLiteral("excited"), QStringLiteral("Excited")},
            {QStringLiteral("gen_z"), QStringLiteral("Gen Z")}};
        QList<WritingProfileSettings> saved = controller->settings()->writingProfileSettings();
        // The mockup's table header, so the two unlabelled columns say which
        // is cleanup and which is tone.
        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(12);
        TextBlock profileHeading = secondaryTextBlock(QStringLiteral("Profile"));
        profileHeading.Width(110);
        TextBlock cleanupHeading = secondaryTextBlock(QStringLiteral("Cleanup"));
        cleanupHeading.Width(140);
        TextBlock toneHeading = secondaryTextBlock(QStringLiteral("Tone"));
        toneHeading.Width(170);
        header.Children().Append(profileHeading);
        header.Children().Append(cleanupHeading);
        header.Children().Append(toneHeading);
        panel.Children().Append(header);
        for (const WritingProfileSettings &entry : defaultWritingProfileSettings()) {
            const WritingProfileSettings current = writingProfileSettingsFor(saved, entry.profile);
            StackPanel row;
            row.Orientation(Orientation::Horizontal);
            row.Spacing(12);
            TextBlock label = textBlock(writingProfileLabel(entry.profile), false);
            label.Width(110);
            label.VerticalAlignment(VerticalAlignment::Center);
            ComboBox cleanupChoice = combo(cleanup, current.cleanupStrength);
            cleanupChoice.MinWidth(140);
            ComboBox toneChoice = combo(tones, current.tone);
            toneChoice.MinWidth(170);
            const auto save = [this, entry, cleanupChoice, toneChoice, cleanup, tones] {
                QList<WritingProfileSettings> values = controller->settings()->writingProfileSettings();
                for (WritingProfileSettings &value : values) {
                    if (value.profile == entry.profile) {
                        value.cleanupStrength = cleanup.at(cleanupChoice.SelectedIndex()).first;
                        value.tone = tones.at(toneChoice.SelectedIndex()).first;
                    }
                }
                controller->settings()->setWritingProfileSettings(values);
            };
            cleanupChoice.SelectionChanged([save](const auto &, const auto &) { save(); });
            toneChoice.SelectionChanged([save](const auto &, const auto &) { save(); });
            row.Children().Append(label);
            row.Children().Append(cleanupChoice);
            row.Children().Append(toneChoice);
            panel.Children().Append(row);
        }
        panel.Children().Append(secondaryTextBlock(QStringLiteral(
            "The default profile is used when Speecher does not recognise the app you are "
            "dictating into. Every profile can be changed later in Settings.")));
        content.Children().Append(panel);
    }

    void showShortcut()
    {
        StackPanel panel = page(
            QStringLiteral("Global Shortcut"),
            QStringLiteral("Tap the shortcut to start dictation and tap it again to stop, or hold it and talk. Dictation ends when you let go."));
        TextBox recorder;
        recorder.IsReadOnly(true);
        recorder.MinWidth(200);
        recorder.PlaceholderText(L"Press a key or key combination…");
        const ShortcutBinding current = controller->globalShortcut();
        recorder.Text(hstring(
            (current.isEmpty() ? ShortcutBinding(WinGlobalShortcutBinder::defaultShortcut())
                               : current)
                .displayText()
                .toStdWString()));
        shortcutStatus = textBlock(QStringLiteral(
            "Press a key combination, or a single key such as Right Alt or F13. "
            "The default is Ctrl+Alt+D."));
        shortcutPendingModifier = 0;

        // The physical key, not the layout's meaning of it: the scancode plus
        // the extended byte is the vocabulary's win column, so bare modifiers
        // record and left is told from right. A key that also types still
        // saves; the status line carries the warning.
        const auto commitSingleKey = [this, recorder](int scanCode) {
            const PhysicalKey *key = physicalKeyForWin(scanCode);
            if (!key) {
                shortcutStatus.Text(L"That key cannot be a dictation key.");
                return;
            }
            const ShortcutBinding binding =
                ShortcutBinding::singleKey(QString::fromLatin1(key->code));
            const QString reason = controller->globalShortcutUnsupportedBindingReason(binding);
            if (!reason.isEmpty()) {
                shortcutStatus.Text(hstring(reason.toStdWString()));
                return;
            }
            QString error;
            if (!controller->setGlobalShortcut(binding, &error)) {
                shortcutStatus.Text(hstring(QStringLiteral("Could not register the shortcut: %1")
                                                .arg(error).toStdWString()));
                return;
            }
            recorder.Text(hstring(binding.displayText().toStdWString()));
            const QString warning = singleKeyTypingWarning(binding);
            shortcutStatus.Text(warning.isEmpty() ? hstring(L"Single key set.")
                                                  : hstring(warning.toStdWString()));
        };
        recorder.KeyDown([this, recorder, commitSingleKey](const auto &,
                                                           const Input::KeyRoutedEventArgs &event) {
            const int virtualKey = static_cast<int>(event.Key());
            // This box records whenever focused, so bare Tab and Enter must
            // keep navigating the wizard rather than silently becoming the
            // shortcut; with a modifier held they are recordable as part of a
            // combination below.
            if ((virtualKey == VK_TAB || virtualKey == VK_RETURN)
                && win::ShortcutRecorder::heldModifiers() == Qt::NoModifier) {
                shortcutPendingModifier = 0;
                return;
            }
            event.Handled(true);
            if (virtualKey == VK_ESCAPE) {
                shortcutPendingModifier = 0;
                return;
            }
            const auto keyStatus = event.KeyStatus();
            if (keyStatus.WasKeyDown) {
                // A held key auto-repeats; only the first press counts.
                return;
            }
            const int scanCode = int(keyStatus.ScanCode)
                | (keyStatus.IsExtendedKey ? 0xE000 : 0);
            if (win::ShortcutRecorder::isModifierKey(virtualKey)) {
                // A lone modifier commits on its release below; a second one
                // makes a modifier-only chord, which is not a valid
                // combination. The exception is AltGr, which Windows delivers
                // as a synthetic Left Ctrl press followed by Right Alt: that
                // pair is one physical key, so Right Alt stays capturable on
                // AltGr layouts.
                const bool altGr = shortcutPendingModifier == 0x1D && scanCode == 0xE038;
                shortcutPendingModifier =
                    shortcutPendingModifier == 0 || altGr ? scanCode : -1;
                return;
            }
            shortcutPendingModifier = -1;
            const Qt::KeyboardModifiers modifiers = win::ShortcutRecorder::heldModifiers();
            if (modifiers == Qt::NoModifier) {
                commitSingleKey(scanCode);
                return;
            }
            // The settings recorder's mapping, so both accept the same keys —
            // F-keys, Space, and the active layout's punctuation included.
            const int qtKey = win::ShortcutRecorder::qtKeyForVirtualKey(virtualKey);
            if (qtKey == 0) {
                shortcutStatus.Text(L"That key cannot be part of a shortcut.");
                return;
            }
            const QKeySequence sequence(QKeyCombination(modifiers, static_cast<Qt::Key>(qtKey)));
            QString error;
            if (!controller->setGlobalShortcut(sequence, &error)) {
                shortcutStatus.Text(hstring(QStringLiteral("Could not register the shortcut: %1")
                                                .arg(error).toStdWString()));
            } else {
                recorder.Text(hstring(sequence.toString(QKeySequence::NativeText).toStdWString()));
                shortcutStatus.Text(L"Shortcut registered.");
            }
        });
        recorder.KeyUp([this, commitSingleKey](const auto &,
                                               const Input::KeyRoutedEventArgs &event) {
            const int virtualKey = static_cast<int>(event.Key());
            // Bare Tab and Enter passed through on the way down; their release
            // must pass through as well.
            if (virtualKey == VK_TAB || virtualKey == VK_RETURN) {
                return;
            }
            event.Handled(true);
            const auto keyStatus = event.KeyStatus();
            const int scanCode = int(keyStatus.ScanCode)
                | (keyStatus.IsExtendedKey ? 0xE000 : 0);
            if (shortcutPendingModifier == scanCode) {
                shortcutPendingModifier = 0;
                commitSingleKey(scanCode);
                return;
            }
            // Once every modifier is up an abandoned or chorded press is
            // over; the next lone modifier can record again.
            if (win::ShortcutRecorder::heldModifiers() == Qt::NoModifier) {
                shortcutPendingModifier = 0;
            }
        });
        // The mockup's "Dictation key" card: the key itself on the right of a
        // single row, with whatever the recorder has to say under it.
        StackPanel keyCard = card(panel, QString());
        keyCard.Children().Append(
            cardRow(glyphMark(L'\uE765'),
                    rowText(textBlock(QStringLiteral("Dictation key"), false)), recorder));
        keyCard.Children().Append(shortcutStatus);

        // The shortcut and its behaviour are set together; the combo shares
        // the shortcuts/activationMode setting the General page's schema row
        // edits rather than keeping a second copy of the value.
        // The wording is the activationMode schema row's, so the wizard and
        // the settings page describe each mode identically.
        const QList<QPair<QString, QString>> modes{
            {shortcutActivationModeName(ShortcutActivationMode::PushToTalk),
             QStringLiteral("Push to talk — dictate only while the key is held")},
            {shortcutActivationModeName(ShortcutActivationMode::Toggle),
             QStringLiteral("Toggle — one press starts, the next press stops")},
            {shortcutActivationModeName(ShortcutActivationMode::Hybrid),
             QStringLiteral("Hybrid — a tap toggles; holding dictates until release")}};
        ComboBox mode = combo(modes,
                              shortcutActivationModeName(
                                  controller->settings()->shortcutActivationMode()));
        mode.SelectionChanged([this, mode, modes](const auto &, const auto &) {
            controller->settings()->setShortcutActivationMode(
                shortcutActivationModeFromName(modes.at(mode.SelectedIndex()).first));
        });
        panel.Children().Append(settingRow(QStringLiteral("Shortcut behaviour"), mode));
        content.Children().Append(panel);
    }

    void showStartAtLogin()
    {
        StackPanel panel = page(
            QStringLiteral("Start at login"),
            QStringLiteral("Dictation only works while Speecher is running."));
        CheckBox launch;
        launch.Content(box_value(L"Start Speecher at login"));
        launch.IsChecked(launchAtLogin);
        launch.Click([this, launch](const auto &, const auto &) {
            launchAtLogin = launch.IsChecked().Value();
        });
        panel.Children().Append(launch);
        content.Children().Append(panel);
    }

    // The name of the input the ready checklist reports, which is the saved
    // device when it is still present and the system default otherwise.
    QString microphoneLabel() const
    {
        const QString id = controller->settings()->audioInputDeviceId();
        if (!id.isEmpty()) {
            for (const AudioInputDeviceInfo &device :
                 controller->platform()->availableAudioInputDevices()) {
                if (device.id == id) {
                    return device.label;
                }
            }
        }
        return QStringLiteral("System default");
    }

    // Why a gated page is still unfinished, in one line, for the Ready page's
    // blocked checklist.
    QString gateReason(int index) const
    {
        switch (index) {
        case 0:
            return QStringLiteral("No ChatGPT or Claude sign-in was found on this computer.");
        case 1:
            return QStringLiteral("%1 is no longer signed in.")
                .arg(providerLabel(controller->providerRegistry()->speechProviders(),
                                   controller->settings()->speechProvider()));
        case 2:
            return QStringLiteral("No microphone input has been detected.");
        default:
            break;
        }
        return QString();
    }

    Grid readyRow(const FrameworkElement &mark, const QString &label, const QString &status,
                  StatusTone tone)
    {
        return cardRow(mark, rowText(textBlock(label)), statusCell(status, tone).root);
    }

    // Either state of the Ready page, redrawn as each re-probe lands rather
    // than left describing a sign-in that has since expired.
    void renderReady()
    {
        if (!readyBody) {
            return;
        }
        readyBody.Children().Clear();
        if (firstUnsatisfiedPage() < 0) {
            renderReadyComplete();
        } else {
            renderReadyBlocked();
        }
    }

    void renderReadyBlocked()
    {
        readyBody.Children().Append(textBlock(QStringLiteral(
            "Speecher can't dictate yet. Finish the steps below, or go back and change your choices.")));
        readyBody.Children().Append(
            strongTextBlock(QStringLiteral("A few steps still need attention:")));
        StackPanel rows = rowList(card(readyBody, QString()));
        for (int index = 0; index <= lastGatedPage; ++index) {
            if (gateSatisfied(index)) {
                continue;
            }
            StackPanel text = rowText(strongTextBlock(SetupWindow::pageTitles().at(index)));
            text.Children().Append(secondaryTextBlock(gateReason(index)));
            Button go;
            go.Content(box_value(L"Go to step"));
            go.VerticalAlignment(VerticalAlignment::Center);
            go.Click([this, index](const auto &, const auto &) { showPage(index); });
            appendRow(rows, cardRow(toneIcon(StatusTone::Caution), text, go));
        }
        readyBody.Children().Append(secondaryTextBlock(QStringLiteral(
            "Finish becomes available once every step above is resolved.")));
    }

    void renderReadyComplete()
    {
        const StatusCell done = statusCell(QStringLiteral("Setup is complete."),
                                           StatusTone::Positive);
        readyBody.Children().Append(done.root);

        // The actual binding, not a hardcoded default: the shortcut step may
        // have recorded anything, a single key included.
        const QString display = controller->globalShortcutDisplay();
        StackPanel how = card(readyBody, QStringLiteral("How to dictate"));
        how.Children().Append(textBlock(
            display.isEmpty()
                ? QStringLiteral("Set a dictation shortcut to start dictating from anywhere.")
                : readyInstruction(controller->settings()->shortcutActivationMode(), display)));
        how.Children().Append(secondaryTextBlock(QStringLiteral(
            "Speecher stays in the notification area. Open its microphone icon for status, your latest transcript, and settings.")));

        StackPanel rows = rowList(card(readyBody, QString()));
        const QString speechId = controller->settings()->speechProvider();
        appendRow(rows, readyRow(brandMark(speechId),
                                 QStringLiteral("Transcription — %1")
                                     .arg(providerLabel(
                                         controller->providerRegistry()->speechProviders(),
                                         speechId)),
                                 QStringLiteral("Ready"), StatusTone::Positive));

        // Refinement is never gated, so this row reports what the refinement
        // page last saw rather than a readiness the Ready page insists on. A
        // provider the walk never reached has no verdict to report.
        const QString refinementId = controller->settings()->refinementProvider();
        QString refinementName = QStringLiteral("None");
        QString refinementStatus = QStringLiteral("No cleanup");
        StatusTone refinementTone = StatusTone::Neutral;
        FrameworkElement refinementMark = glyphMark(L'');
        if (refinementId != QStringLiteral("none")) {
            refinementName = providerLabel(
                controller->providerRegistry()->refinementProviders(), refinementId);
            refinementMark = brandMark(refinementId);
            if (!refinementReady.contains(refinementId)) {
                refinementStatus = QStringLiteral("Not checked");
            } else if (refinementReady.value(refinementId)) {
                refinementStatus = QStringLiteral("Ready");
                refinementTone = StatusTone::Positive;
            } else {
                refinementStatus = QStringLiteral("Not signed in");
                refinementTone = StatusTone::Caution;
            }
        }
        appendRow(rows, readyRow(refinementMark,
                                 QStringLiteral("Refinement — %1").arg(refinementName),
                                 refinementStatus, refinementTone));

        appendRow(rows, readyRow(glyphMark(L'\uE720'),
                                 QStringLiteral("Microphone — %1").arg(microphoneLabel()),
                                 QStringLiteral("Ready"), StatusTone::Positive));
        appendRow(rows, readyRow(glyphMark(L'\uE765'),
                                 QStringLiteral("Text delivery — Windows clipboard"),
                                 QStringLiteral("Ready"), StatusTone::Positive));
    }

    void showReady()
    {
        StackPanel panel = page(QStringLiteral("Ready to dictate"), QString());
        readyBody = StackPanel();
        readyBody.Spacing(12);
        panel.Children().Append(readyBody);
        content.Children().Append(panel);
        renderReady();

        // Finishing must not trust what the welcome and transcription pages saw
        // however long ago: a sign-in can expire while the wizard sits on a
        // later page. Every provider is probed, because the welcome gate asks
        // whether any is signed in and the transcription gate asks about the
        // selected one; both read speechReady, and refreshGates re-reads them
        // as each verdict lands.
        const quint64 generation = ++checkGeneration;
        for (const ProviderDescriptor &provider :
             controller->providerRegistry()->speechProviders()) {
            probeSpeechProvider(provider.id, generation,
                                [this, id = provider.id](const SpeechPrepareResult &result) {
                speechReady.insert(id, result.ok);
                refreshGates();
                renderReady();
            });
        }
    }

    void suspendShortcut()
    {
        if (shortcutSuspended) {
            return;
        }
        shortcutSuspended = true;
        controller->suspendGlobalShortcut();
    }

    void resumeShortcut()
    {
        if (!shortcutSuspended) {
            return;
        }
        shortcutSuspended = false;
        const QString error = controller->resumeGlobalShortcut();
        if (error.isEmpty()) {
            return;
        }
        if (shortcutStatus) {
            shortcutStatus.Text(hstring(QStringLiteral("Could not register the shortcut: %1")
                                            .arg(error).toStdWString()));
        } else {
            qWarning().noquote() << "Could not restore the Global Shortcut:" << error;
        }
    }

    void complete(bool skipped)
    {
        microphone->stop();
        // Finishing re-checks every gate: a prerequisite can break after its
        // page was passed. Skip is only offered while they all hold, and the
        // single-page shortcut recorder answers for none of them.
        if (!skipped && !singlePage) {
            const int unsatisfied = firstUnsatisfiedPage();
            if (unsatisfied >= 0) {
                showPage(unsatisfied);
                return;
            }
        }
        // Skipping applies the same two settings finishing would; a skipped
        // setup that registers no shortcut leaves nothing to dictate with.
        controller->settings()->setLaunchAtLogin(launchAtLogin);
        // Always register, not only when nothing is saved: a combination
        // another app already owns is stored happily and does nothing.
        const ShortcutBinding saved = controller->globalShortcut();
        const ShortcutBinding effective = saved.isEmpty()
            ? ShortcutBinding(WinGlobalShortcutBinder::defaultShortcut())
            : saved;
        QString error;
        if (!controller->setGlobalShortcut(effective, &error)) {
            showPage(shortcutPage);
            // Only a conflict is answered by recording something else. A key
            // Windows cannot register at all needs its own reason said out
            // loud, or the user retypes the same chord forever.
            const QString message =
                WinGlobalShortcutBinder::describesConflict(error) || error.isEmpty()
                    ? QStringLiteral("Another app is using %1. Record a different shortcut.")
                          .arg(effective.displayText())
                    : error;
            shortcutStatus.Text(hstring(message.toStdWString()));
            return;
        }
        controller->settings()->setSetupCompleted(true);
        window.Close();
        if (!controller->popupOnly()) {
            controller->showSettingsWindow();
        }
    }

    ApplicationController *controller;
    std::function<void()> firstFrame;
    SetupWindow *setup;
    Window window{nullptr};
    StackPanel content{nullptr};
    Button skip{nullptr};
    Button back{nullptr};
    Button next{nullptr};
    AudioInput *microphone = nullptr;
    ProgressBar microphoneLevel{nullptr};
    TextBlock microphoneStatus{nullptr};
    InfoBar microphoneProblem{nullptr};
    TextBlock shortcutStatus{nullptr};
    // The Ready page's body, redrawn in place as its re-probes land, and null
    // whenever another page is on screen.
    StackPanel readyBody{nullptr};
    // Discards the results of a check the wizard has moved on from.
    quint64 checkGeneration = 0;
    // What the last probe said about each provider, by id: the welcome and
    // transcription gates read these rather than probing again.
    QHash<QString, bool> speechReady;
    QHash<QString, QString> speechMessage;
    QHash<QString, bool> refinementReady;
    // Set once a provider row has been chosen, by the user or by the one
    // auto-selection each list is allowed per wizard run.
    bool speechSelectionSettled = false;
    bool refinementSelectionSettled = false;
    // The index this code last wrote to each RadioButtons and has not yet seen
    // reported back, or -1. WinUI holds the initial SelectedIndex until its
    // item repeater loads and only then raises SelectionChanged, so a flag
    // cleared straight after the write would already be false when the event
    // lands — and the wizard would read its own write as the user's choice.
    int programmaticSpeechIndex = -1;
    int programmaticRefinementIndex = -1;
    // Latched by the level meter: the microphone gate asks whether this
    // device has ever been heard, and starting a meter clears it again.
    bool microphoneDetected = false;
    int pageIndex = 0;
    bool launchAtLogin;
    bool singlePage = false;
    bool shortcutSuspended = false;
    // The recorder's pending lone modifier: its scancode (with the extended
    // byte) while it alone is down, 0 when none, -1 once another key joined
    // it — a modifier-only chord must not commit on release.
    int shortcutPendingModifier = 0;
};

SetupWindow::SetupWindow(ApplicationController *controller,
                         std::function<void()> firstFrame,
                         QObject *parent)
    : QObject(parent)
    , m_native(std::make_unique<Native>(controller, std::move(firstFrame), this))
{
}

SetupWindow::~SetupWindow() = default;

void SetupWindow::show(SetupAssistantPage page)
{
    m_native->show(page);
}

QStringList SetupWindow::pageTitles()
{
    return {QStringLiteral("Welcome to Speecher"),
            QStringLiteral("Transcription"),
            QStringLiteral("Microphone"),
            QStringLiteral("Text delivery"),
            QStringLiteral("Refinement"),
            QStringLiteral("Writing profiles"),
            QStringLiteral("Global Shortcut"),
            QStringLiteral("Start at login"),
            QStringLiteral("Ready to dictate")};
}

void SetupWindow::skipForTest()
{
    m_native->complete(true);
}

QString SetupWindow::currentPageTitleForTest() const
{
    return pageTitles().at(m_native->pageIndex);
}

QStringList SetupWindow::welcomeCopyForTest()
{
    return welcomeCopy();
}

} // namespace speecher
