#include "frontend/win/SetupWindow.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PlatformComposition.h"
#include "core/AppSettings.h"
#include "core/EndpointSettings.h"
#include "core/OutputFormat.h"
#include "core/SettingsStore.h"
#include "core/ShortcutBinding.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "frontend/win/LocalModelBrowser.h"
#include "frontend/win/SettingsPage.h"
#include "frontend/win/ShortcutRecorder.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "providers/CustomEndpoints.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderProbe.h"
#include "providers/ProviderRegistry.h"
#include "providers/ProviderSignIn.h"

#include <windows.h>
#include <shellapi.h>
#include <microsoft.ui.xaml.window.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
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
#include <QPointer>
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
using Microsoft::UI::Xaml::Automation::AutomationProperties;
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

const QString kLocal = QStringLiteral("local");
const QString kEndpoint = QStringLiteral("endpoint");
const QString kNone = QStringLiteral("none");
// Segoe Fluent Icons: a desktop PC for this computer, a contact for a sign-in,
// a network for a server someone runs.
constexpr wchar_t kComputerGlyph = L'\uE977';
constexpr wchar_t kContactGlyph = L'\uE77B';
constexpr wchar_t kServerGlyph = L'\uE968';

void setShown(const UIElement &element, bool shown)
{
    element.Visibility(shown ? Visibility::Visible : Visibility::Collapsed);
}

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

    // An empty value hides the whole cell, glyph included.
    void set(const QString &value, StatusTone tone) const
    {
        root.Visibility(value.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
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
void appendRow(const StackPanel &list, const UIElement &row)
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

// One choice on the Refinement step. The groups are separate cards, so the
// choice is RadioButtons sharing a GroupName rather than one RadioButtons.
struct RefinementOption {
    QString id;
    QString label;
    RadioButton button{nullptr};
    StatusCell status;
};

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
        // Speech on this computer is only a path where the build can run it.
        if (controller->providerRegistry()->speechProvider(kLocal)) {
            localSpeech = controller->localSetup();
        }
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
            clearPage();
            resumeShortcut();
            window = nullptr;
            content = nullptr;
            skip = back = next = nullptr;
        });

        Grid root;
        root.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        // Rating badges pick their brushes by this window's theme.
        themeHost.effectiveTheme = [root] { return root.ActualTheme(); };
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
        // A new run of the assistant starts with no path chosen.
        if (!window) {
            welcomeChoice = WelcomeChoice();
        }
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
            // Welcome: running on this computer was chosen, or at least one
            // provider sign-in is on this machine, or a usable CLI Proxy API
            // account the Transcription step can opt into.
            return (localSpeech && welcomeChoice.local()) || anySignInFound();
        case 1:
            // A Local Model counts once its download has started: it keeps
            // going while setup continues.
            if (localSelected()) {
                return localDownloadStarted();
            }
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

    // speechReady and speechMessage answer together: the Transcription page
    // treats a recorded verdict as checked and paints its message in a verdict
    // tone, so a probe that stored one without the other would present
    // "Checking…" as if it were the verdict.
    void recordSpeechVerdict(const QString &id, const SpeechPrepareResult &result)
    {
        QString label = id;
        for (const ProviderDescriptor &provider :
             controller->providerRegistry()->speechProviders()) {
            if (provider.id == id) {
                label = provider.label;
                break;
            }
        }
        speechReady.insert(id, result.ok);
        speechMessage.insert(id, result.ok ? QStringLiteral("%1 is ready.").arg(label)
                                           : result.message);
    }

    // A speech provider's credential probe, off the UI thread where the
    // provider offers a job, reported back on it. A result is discarded only
    // when a newer probe of the same provider superseded it; leaving the page
    // does not — a late verdict still lands in the shared readiness maps, but
    // report, which draws on the page that asked, runs only while that page is
    // on screen.
    void probeSpeechProvider(const QString &id,
                             quint64 generation,
                             std::function<void(const SpeechPrepareResult &)> report)
    {
        // Superseded per provider, not per wizard: re-probing one provider's
        // changed sign-in must not strand the others' in-flight verdicts.
        speechProbeGeneration.insert(id, generation);
        const auto land = [this, id, page = QPointer<QObject>(pageScope.get()),
                           report](const SpeechPrepareResult &result) {
            recordSpeechVerdict(id, result);
            if (page) {
                report(result);
            }
        };
        SpeechTranscriber *transcriber = controller->providerRegistry()->speechProvider(id);
        if (!transcriber) {
            land({false, QStringLiteral("No transcription service is available.")});
            return;
        }
        const SpeechSettings settings = controller->settings()->snapshot().speech;
        std::optional<SpeechPrepareJob> job = transcriber->createPrepareJob(settings);
        if (!job || !job->run) {
            land(transcriber->prepare(settings));
            return;
        }
        auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
        runProviderProbe<SpeechPrepareResult>(
            controller->providerRegistry(), setup,
            [prepareJob] { return prepareJob->run(); },
            [this, id, generation, prepareJob, land](const SpeechPrepareResult &result) {
                if (generation != speechProbeGeneration.value(id)) {
                    return;
                }
                if (prepareJob->apply) {
                    prepareJob->apply(result);
                }
                land(result);
            });
    }

    // The refinement equivalent: the refiner's own credential resolution,
    // which is all "ready" can mean before a dictation happens.
    void probeRefinementProvider(const QString &id,
                                 quint64 generation,
                                 std::function<void(bool)> report)
    {
        refinementProbeGeneration.insert(id, generation);
        const auto land = [this, id, page = QPointer<QObject>(pageScope.get()), report](bool ok) {
            refinementReady.insert(id, ok);
            if (page) {
                report(ok);
            }
        };
        TranscriptRefiner *refiner = controller->providerRegistry()->refinementProvider(id);
        if (!refiner) {
            land(false);
            return;
        }
        const RefinementSettings settings = controller->settings()->snapshot().refinement;
        std::optional<RefinementRefreshJob> job = refiner->createRefreshJob(settings);
        if (!job || !job->run) {
            land(refiner->prepare(settings).ok);
            return;
        }
        auto refreshJob = std::make_shared<RefinementRefreshJob>(std::move(*job));
        runProviderProbe<RefinementRefreshResult>(
            controller->providerRegistry(), setup,
            [refreshJob] { return refreshJob->run(); },
            [this, id, generation, refreshJob, land](const RefinementRefreshResult &result) {
                if (generation != refinementProbeGeneration.value(id)) {
                    return;
                }
                if (refreshJob->apply) {
                    refreshJob->apply(result);
                }
                land(result.ok);
            });
    }

    // Forgets the page on screen: its controls, its callbacks and its Qt
    // connections, so nothing that answers later draws on a page that is gone.
    void clearPage()
    {
        microphone->stop();
        microphoneLevel = nullptr;
        microphoneStatus = nullptr;
        microphoneProblem = nullptr;
        shortcutStatus = nullptr;
        readyBody = nullptr;
        readyDownload = {};
        localCard = {};
        localRowStatus = {};
        runner = {};
        endpointForm = {};
        refinementRefresh = nullptr;
        transcriptionRefresh = nullptr;
        pageScope = std::make_unique<QObject>();
        ++checkGeneration;
    }

    void showPage(int index)
    {
        if (index < 0 || index >= SetupWindow::pageTitles().size()) {
            return;
        }
        clearPage();
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

    // The providers a sign-in answers for. Local models and a speech server
    // are set up on their own pages, not signed in to.
    QList<ProviderDescriptor> signInProviders() const
    {
        QList<ProviderDescriptor> providers = controller->providerRegistry()->speechProviders();
        providers.removeIf([](const ProviderDescriptor &provider) {
            return !isSetupSignInProvider(provider.id);
        });
        return providers;
    }

    bool anySignInFound() const
    {
        for (const ProviderDescriptor &provider : signInProviders()) {
            if (speechReady.value(provider.id, false)) {
                return true;
            }
        }
        return cliproxyReady;
    }

    // Every speech provider this assistant sets.
    void setSpeechProvider(const QString &id)
    {
        controller->settings()->setSpeechProvider(id);
    }

    // The Welcome path, the person's when choice is given and otherwise the
    // default the completed checks suggest. WelcomeChoice decides the speech
    // provider that goes with it, including undoing a default it wrote.
    void choosePath(std::optional<bool> choice = std::nullopt)
    {
        QStringList ready;
        for (const ProviderDescriptor &provider : signInProviders()) {
            if (speechReady.value(provider.id, false)) {
                ready.append(provider.id);
            }
        }
        const QString current = controller->settings()->speechProvider();
        const QString provider = welcomeChoice.update(current, ready, cliproxyReady, choice);
        if (provider != current) {
            setSpeechProvider(provider);
        }
        refreshGates();
    }

    // A line with the computer glyph and what the hardware probe found.
    StackPanel hardwareLine(TextBlock &text)
    {
        StackPanel line;
        line.Orientation(Orientation::Horizontal);
        line.Spacing(8);
        FontIcon icon = glyphMark(kComputerGlyph);
        icon.FontSize(14);
        line.Children().Append(icon);
        text = textBlock(localSpeech->hardwareLine());
        line.Children().Append(text);
        return line;
    }

    void showWelcome()
    {
        const QStringList copy = welcomeCopy();
        StackPanel panel = page(
            QStringLiteral("Welcome to Speecher"),
            copy.at(0));
        panel.Children().Append(textBlock(copy.at(1)));

        // Two ways into dictation: a sign-in the person already has, or a
        // model on this computer. The sign-in rows below only matter for the
        // first.
        RadioButtons paths{nullptr};
        StatusCell signInPathStatus;
        StackPanel localDetail{nullptr};
        StackPanel signInDetail;
        signInDetail.Spacing(12);
        if (localSpeech) {
            panel.Children().Append(
                strongTextBlock(QStringLiteral("How should Speecher turn speech into text?")));
            paths = RadioButtons();
            const auto addPath = [&paths](wchar_t glyph, const QString &title, const QString &note,
                                          const FrameworkElement &trailing) {
                StackPanel text = rowText(strongTextBlock(title));
                text.Children().Append(secondaryTextBlock(note));
                Grid item = cardRow(glyphMark(glyph), text, trailing);
                item.MinWidth(choiceRowWidth);
                AutomationProperties::SetName(item, win::hs(title));
                paths.Items().Append(item);
            };
            signInPathStatus = statusCell(QStringLiteral("Checking…"), StatusTone::Neutral);
            addPath(kContactGlyph, QStringLiteral("Use my ChatGPT or Claude sign-in"),
                    QStringLiteral("Transcribed in the cloud by the service you already pay for."),
                    signInPathStatus.root);
            addPath(kComputerGlyph, QStringLiteral("Run on this computer"),
                    QStringLiteral("Private, no account, works offline after a one-time download."),
                    FrameworkElement{nullptr});
            paths.SelectedIndex(welcomeChoice.local() ? 1 : 0);
            panel.Children().Append(paths);

            localDetail = StackPanel();
            localDetail.Spacing(4);
            TextBlock hardware{nullptr};
            localDetail.Children().Append(hardwareLine(hardware));
            localDetail.Children().Append(textBlock(QStringLiteral(
                "Speecher will suggest a speech model for this computer on the next step. "
                "Dictation stays on this computer and works offline once the model is downloaded.")));
            panel.Children().Append(localDetail);
            QObject::connect(localSpeech, &LocalSetup::changed, pageScope.get(), [this, hardware] {
                hardware.Text(win::hs(localSpeech->hardwareLine()));
            });
            localSpeech->probeHardware();
        }
        panel.Children().Append(signInDetail);
        const auto showPath = [localDetail, signInDetail, this] {
            if (localDetail) {
                setShown(localDetail, welcomeChoice.local());
                setShown(signInDetail, !welcomeChoice.local());
            }
        };
        if (paths) {
            // The page selects the path WelcomeChoice holds, on building and
            // after checks; only a move away from it is the person's choice.
            // Treating the echo as a choice once sent Back to Welcome through
            // a provider write the person never made.
            paths.SelectionChanged([this, paths, showPath](const auto &, const auto &) {
                const int index = paths.SelectedIndex();
                if (index >= 0 && (index == 1) != welcomeChoice.local()) {
                    choosePath(index == 1);
                }
                showPath();
            });
        }
        showPath();
        // Until the person picks, every round of checks sets the default:
        // the sign-in when one is found, else this computer.
        const auto checksAnswered = [this, paths, showPath] {
            if (!paths) {
                return;
            }
            choosePath();
            const int wanted = welcomeChoice.local() ? 1 : 0;
            if (paths.SelectedIndex() != wanted) {
                paths.SelectedIndex(wanted);
            }
            showPath();
        };
        const auto showSignInPath = [this, signInPathStatus] {
            if (!signInPathStatus.root) {
                return;
            }
            const bool found = anySignInFound();
            signInPathStatus.set(found ? QStringLiteral("Sign-in found") : QStringLiteral("None found"),
                                 found ? StatusTone::Positive : StatusTone::Neutral);
        };

        StackPanel before = card(signInDetail, localSpeech ? QStringLiteral("Sign-ins on this computer")
                                                           : QStringLiteral("Before you start"));
        before.Children().Append(secondaryTextBlock(QStringLiteral(
            "Speecher uses your existing ChatGPT or Claude sign-in, or an account saved by "
            "CLI Proxy API. Sign in to one of these, then choose Check again:")));
        StackPanel list = rowList(before);

        QStringList ids;
        std::vector<StatusCell> statuses;
        std::vector<TextBlock> hints;
        for (const ProviderDescriptor &provider : signInProviders()) {
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

        // Accounts saved by CLI Proxy API count as a sign-in of their own:
        // someone whose only login lives there opts in on the Transcription
        // step. The directory is enterable right here, because a user whose
        // accounts live in a custom directory would otherwise be held on this
        // page with the field that could free them gated behind Next.
        TextBlock cliproxyHint = secondaryTextBlock(QString());
        TextBox cliproxyDir;
        cliproxyDir.Text(win::hs(signIn.configuredAccountDirectory()));
        cliproxyDir.Visibility(Visibility::Collapsed);
        StackPanel cliproxyText = rowText(textBlock(QStringLiteral("CLI Proxy API")));
        cliproxyText.Children().Append(cliproxyHint);
        cliproxyText.Children().Append(cliproxyDir);
        const StatusCell cliproxyStatus = statusCell(QStringLiteral("Checking…"),
                                                     StatusTone::Neutral);
        appendRow(list, cardRow(FrameworkElement{nullptr}, cliproxyText, cliproxyStatus.root));

        const auto checkCliproxy = [this, ids, cliproxyStatus, cliproxyHint, cliproxyDir,
                                    showSignInPath] {
            cliproxyDir.PlaceholderText(win::hs(signIn.resolvedAccountDirectory()));
            cliproxyReady = signIn.anyUsableAccount(ids);
            cliproxyStatus.set(cliproxyReady ? QStringLiteral("Accounts found")
                                             : QStringLiteral("Not found"),
                               cliproxyReady ? StatusTone::Positive : StatusTone::Neutral);
            // Unlike the provider rows, the found state is the one that needs
            // a next step: the sign-in must be switched on the Transcription
            // step or its probes will fail against the CLI sign-ins.
            cliproxyHint.Text(win::hs(cliproxyReady
                ? ProviderSignIn::cliproxyAccountsFoundHint()
                : ProviderSignIn::cliproxyAccountsMissingHint()));
            cliproxyDir.Visibility(!cliproxyReady
                                           || cliproxyDir.FocusState() != FocusState::Unfocused
                                       ? Visibility::Visible
                                       : Visibility::Collapsed);
            showSignInPath();
            refreshGates();
        };
        const auto commitCliproxyDir = [this, cliproxyDir, checkCliproxy] {
            const QString directory = win::qs(cliproxyDir.Text()).trimmed();
            if (directory == signIn.configuredAccountDirectory()) {
                return;
            }
            signIn.setAccountDirectory(directory);
            checkCliproxy();
        };
        cliproxyDir.LostFocus([commitCliproxyDir](const auto &, const auto &) {
            commitCliproxyDir();
        });
        cliproxyDir.KeyDown([commitCliproxyDir](const auto &,
                                                const Input::KeyRoutedEventArgs &args) {
            if (args.Key() == Windows::System::VirtualKey::Enter) {
                commitCliproxyDir();
            }
        });

        Button check;
        check.Content(box_value(L"Check again"));
        check.HorizontalAlignment(HorizontalAlignment::Left);
        const auto runChecks = [this, ids, statuses, hints, checkCliproxy, checksAnswered,
                                showSignInPath] {
            const quint64 generation = ++checkGeneration;
            checkCliproxy();
            auto outstanding = std::make_shared<qsizetype>(ids.size());
            for (int index = 0; index < ids.size(); ++index) {
                const StatusCell status = statuses.at(size_t(index));
                const TextBlock hint = hints.at(size_t(index));
                status.set(QStringLiteral("Checking…"), StatusTone::Neutral);
                probeSpeechProvider(ids.at(index), generation,
                                    [this, id = ids.at(index), status, hint, outstanding,
                                     checksAnswered, showSignInPath](
                                        const SpeechPrepareResult &result) {
                    status.set(result.ok ? QStringLiteral("Sign-in found")
                                         : QStringLiteral("Not found"),
                               result.ok ? StatusTone::Positive : StatusTone::Neutral);
                    hint.Visibility(result.ok ? Visibility::Collapsed : Visibility::Visible);
                    showSignInPath();
                    refreshGates();
                    if (--*outstanding == 0) {
                        checksAnswered();
                    }
                });
            }
            if (ids.isEmpty()) {
                checksAnswered();
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

    // Once per wizard run, and never over a choice made here: a saved sign-in
    // whose probe failed gives way to one whose probe succeeded, by the rule
    // the other assistants share.
    void autoSelectSpeechProvider(const RadioButtons &choices,
                                  const QList<QPair<QString, QString>> &options)
    {
        const QString saved = controller->settings()->speechProvider();
        if (speechSelectionSettled || !speechReady.contains(saved)) {
            return;
        }
        QStringList ready;
        for (const auto &option : options) {
            if (speechReady.value(option.first, false)) {
                ready.append(option.first);
            }
        }
        const QString chosen = setupProviderChoice(saved, ready, false);
        for (int index = 0; index < options.size(); ++index) {
            if (options.at(index).first != chosen || chosen == saved) {
                continue;
            }
            // Persisted here rather than left to the selection handler, so the
            // switch holds whether or not the control reports it.
            speechSelectionSettled = true;
            setSpeechProvider(chosen);
            selectProgrammatically(choices, programmaticSpeechIndex, index);
            return;
        }
    }

    bool localSelected() const
    {
        return localSpeech && controller->settings()->speechProvider() == kLocal;
    }

    // The model the Local card shows, which is the one dictation will use.
    const LocalModel &localChoice() const
    {
        return localSpeech->speechModelChoice();
    }

    // Picking in the compare table is a choice the suggestion never
    // overrides; chooseSpeechModel announces it, which redraws the page.
    void setLocalChoice(const QString &modelId)
    {
        localSpeech->chooseSpeechModel(modelId);
    }

    bool localDownloadStarted() const
    {
        const auto state = localSpeech->modelState(localChoice());
        return state.downloaded || state.downloading;
    }

    // The Local card: the hardware line, the suggested model with its facts
    // and Download, and the comparison table behind an Expander.
    StackPanel makeLocalSection()
    {
        LocalCard &card = localCard;
        card.section = StackPanel();
        card.section.Spacing(8);
        card.section.Children().Append(hardwareLine(card.hardware));

        StackPanel text;
        text.Spacing(2);
        card.caption = secondaryTextBlock(QString());
        text.Children().Append(card.caption);
        StackPanel title;
        title.Orientation(Orientation::Horizontal);
        title.Spacing(8);
        card.name = strongTextBlock(QString());
        card.name.VerticalAlignment(VerticalAlignment::Center);
        title.Children().Append(card.name);
        // The badge is rebuilt with each model, so it sits in a holder.
        card.rating = Border();
        title.Children().Append(card.rating);
        text.Children().Append(title);
        card.facts = textBlock(QString());
        text.Children().Append(card.facts);

        StackPanel action;
        action.Spacing(6);
        action.VerticalAlignment(VerticalAlignment::Center);
        card.download = Button();
        card.download.HorizontalAlignment(HorizontalAlignment::Right);
        action.Children().Append(card.download);
        card.progress = ProgressBar();
        card.progress.Width(160);
        card.progress.Maximum(1000);
        action.Children().Append(card.progress);
        card.state = secondaryTextBlock(QString());
        card.state.HorizontalAlignment(HorizontalAlignment::Right);
        action.Children().Append(card.state);
        card.cancel = Button();
        card.cancel.Content(box_value(L"Cancel"));
        card.cancel.HorizontalAlignment(HorizontalAlignment::Right);
        action.Children().Append(card.cancel);
        StackPanel body = ::speecher::card(card.section, QString());
        body.Children().Append(cardRow(FrameworkElement{nullptr}, text, action));

        // The comparison, as the settings collections draw a table: a header
        // grid over a ListView whose rows share its columns.
        const auto columnGrid = [] {
            Grid grid;
            grid.ColumnSpacing(8);
            for (const GridLength width : {GridLength{1, GridUnitType::Star}, GridLength{72, GridUnitType::Pixel},
                                           GridLength{92, GridUnitType::Pixel}, GridLength{92, GridUnitType::Pixel},
                                           GridLength{96, GridUnitType::Pixel}, GridLength{72, GridUnitType::Pixel}}) {
                ColumnDefinition column;
                column.Width(width);
                grid.ColumnDefinitions().Append(column);
            }
            return grid;
        };
        StackPanel table;
        table.Spacing(4);
        Grid header = columnGrid();
        header.Padding({12, 0, 12, 0});
        const QStringList titles{QStringLiteral("Model"), QStringLiteral("Download"),
                                 QStringLiteral("Word errors"), QStringLiteral("10 s of speech"),
                                 QStringLiteral("Text shows"), QStringLiteral("Memory")};
        for (int column = 0; column < titles.size(); ++column) {
            TextBlock title = secondaryTextBlock(titles.at(column));
            Grid::SetColumn(title, column);
            header.Children().Append(title);
        }
        table.Children().Append(header);
        card.compare = ListView();
        card.compare.SelectionMode(ListViewSelectionMode::Single);
        AutomationProperties::SetName(card.compare, L"Speech models");
        Style container(xaml_typename<ListViewItem>());
        container.Setters().Append(Setter(Control::HorizontalContentAlignmentProperty(),
                                          box_value(HorizontalAlignment::Stretch)));
        container.Setters().Append(Setter(Control::PaddingProperty(), box_value(Thickness{12, 4, 12, 4})));
        card.compare.ItemContainerStyle(container);
        for (const LocalModel &model : localModelCatalog()) {
            // Two lines: the name, and its rating under it, as the model
            // column is too narrow for both on one; the facts span both.
            Grid row = columnGrid();
            row.RowSpacing(2);
            row.RowDefinitions().Append(RowDefinition());
            row.RowDefinitions().Append(RowDefinition());
            for (int column = 0; column < titles.size(); ++column) {
                // The name and the speed ("~0.7 s (estimated)") wrap rather than clip.
                TextBlock cell = textBlock(QString(), column == 0 || column == 3);
                cell.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(cell, column);
                Grid::SetRowSpan(cell, column == 0 ? 1 : 2);
                row.Children().Append(cell);
            }
            // After the cells, so they keep their indices.
            Grid rating = win::ratingBadge(model.rating, themeHost);
            Grid::SetRow(rating, 1);
            row.Children().Append(rating);
            AutomationProperties::SetName(row, win::hs(model.name));
            card.compare.Items().Append(row);
        }
        table.Children().Append(card.compare);
        table.Children().Append(secondaryTextBlock(QStringLiteral(
            "Word errors: clear read speech / everyday speech. Times are estimates until a model is "
            "downloaded and tested here.")));
        Expander compare;
        compare.Header(box_value(win::hs(QStringLiteral("Compare %1 other models")
                                             .arg(localModelCatalog().size() - 1))));
        compare.HorizontalAlignment(HorizontalAlignment::Stretch);
        compare.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        compare.Content(table);
        card.section.Children().Append(compare);

        card.download.Click([this](const auto &, const auto &) {
            const LocalModel &model = localChoice();
            localSpeech->chooseSpeechModel(model.id);
            localSpeech->download(model);
        });
        card.cancel.Click([this](const auto &, const auto &) { localSpeech->cancelDownload(localChoice().id); });
        // showLocalChoice re-selects the current choice after every refresh;
        // only a move to another row is the person's.
        card.compare.SelectionChanged([this](const winrt::Windows::Foundation::IInspectable &sender, const auto &) {
            const int index = sender.as<ListView>().SelectedIndex();
            if (index >= 0 && index < localModelCatalog().size()
                && localModelCatalog().at(index).id != localChoice().id) {
                setLocalChoice(localModelCatalog().at(index).id);
            }
        });
        return card.section;
    }

    void showLocalChoice()
    {
        LocalCard &card = localCard;
        if (!card.section) {
            return;
        }
        setShown(card.section, localSelected());
        const LocalModel &model = localChoice();
        const LocalSetup::ModelState state = localSpeech->modelState(model);
        card.hardware.Text(win::hs(localSpeech->hardwareLine()));
        card.caption.Text(state.suggested ? L"Suggested for this computer" : L"Your choice");
        card.name.Text(win::hs(model.name));
        card.rating.Child(win::ratingBadge(model.rating, themeHost));
        card.facts.Text(win::hs(state.cardFacts));
        card.download.IsEnabled(!state.tooLarge);
        card.download.Content(box_value(win::hs(state.tooLarge ? QStringLiteral("Too large for this computer")
                                                               : QStringLiteral("Download %1")
                                                                     .arg(downloadSizeText(model.sizeBytes)))));

        const QList<LocalModel> &catalog = localModelCatalog();
        int selected = -1;
        for (int row = 0; row < catalog.size(); ++row) {
            const LocalModel &entry = catalog.at(row);
            const QStringList cells = localSpeech->modelState(entry).tableCells;
            const auto rowCells = card.compare.Items().GetAt(uint32_t(row)).as<Grid>().Children();
            for (int column = 0; column < cells.size(); ++column) {
                rowCells.GetAt(uint32_t(column)).as<TextBlock>().Text(win::hs(cells.at(column)));
            }
            if (entry.id == model.id) {
                selected = row;
            }
        }
        if (card.compare.SelectedIndex() != selected) {
            card.compare.SelectedIndex(selected);
        }
        showLocalDownload();
    }

    // The part of the Local card a download's progress moves, many times a
    // second, without touching the rest.
    void showLocalDownload()
    {
        LocalCard &card = localCard;
        if (!card.section) {
            return;
        }
        const LocalModel &model = localChoice();
        const auto progress = localSpeech->downloadProgress(model.id);
        const auto modelState = localSpeech->modelState(model);
        const bool downloaded = modelState.downloaded;
        setShown(card.download, !progress && !downloaded);
        setShown(card.progress, bool(progress));
        setShown(card.cancel, bool(progress));
        QString state;
        if (progress) {
            card.progress.Value(progress->second > 0 ? double(progress->first * 1000 / progress->second) : 0);
            state = QStringLiteral("%1 of %2").arg(downloadSizeText(progress->first),
                                                   downloadSizeText(model.sizeBytes));
        } else if (downloaded) {
            state = QStringLiteral("Downloaded");
        } else {
            state = modelState.problem;
        }
        card.state.Text(win::hs(state));
        setShown(card.state, !state.isEmpty());
        if (localRowStatus.root) {
            localRowStatus.set(downloaded ? QStringLiteral("Ready")
                               : progress ? QStringLiteral("Downloading")
                                          : QString(),
                               downloaded ? StatusTone::Positive : StatusTone::Neutral);
        }
    }

    void showTranscription()
    {
        StackPanel panel = page(
            QStringLiteral("Transcription"),
            QStringLiteral("Choose the service Speecher uses to turn speech into a raw transcript."));
        QList<QPair<QString, QString>> options;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->speechProviders()) {
            // The Local card is only a choice where the assistant can set it
            // up, and a speech server is set up in Settings alone.
            if (!offersSetupSpeechProvider(provider.id, controller->settings()->speechProvider(), localSpeech != nullptr)) {
                continue;
            }
            options.append({provider.id, provider.label});
        }
        // Every service is on screen with its own readiness, rather than one
        // hidden behind a dropdown.
        RadioButtons choices;
        std::vector<StatusCell> statuses;
        int selectedIndex = 0;
        for (int index = 0; index < options.size(); ++index) {
            const QString id = options.at(index).first;
            const bool local = id == kLocal;
            // The Local row's status is its download, which showLocalChoice keeps.
            const StatusCell status = statusCell(local ? QString() : QStringLiteral("Checking…"),
                                                 StatusTone::Neutral);
            StackPanel text = rowText(strongTextBlock(options.at(index).second));
            if (local) {
                text.Children().Append(secondaryTextBlock(
                    QStringLiteral("Runs on this computer. No account, works offline.")));
                localRowStatus = status;
            }
            Grid item = cardRow(local ? FrameworkElement(glyphMark(kComputerGlyph)) : brandMark(id),
                                text, status.root);
            AutomationProperties::SetName(item, win::hs(options.at(index).second));
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
        // The Sign-in card: the service's own sign-in stays the silent
        // default, and CLI Proxy API is the exception this checkbox opts into,
        // revealing the account and directory rows. Mirrors the Qt assistant.
        StackPanel signInBody;
        signInBody.Spacing(8);
        signInBody.Margin({16, 14, 16, 14});
        signInBody.Children().Append(strongTextBlock(QStringLiteral("Sign-in")));
        CheckBox useCliproxy = wrappingCheckBox(ProviderSignIn::cliproxyOptInLabel());
        signInBody.Children().Append(useCliproxy);
        ComboBox cliproxyAccount;
        cliproxyAccount.MinWidth(240);
        StackPanel accountRow = settingRow(QStringLiteral("CLI Proxy API account"),
                                           cliproxyAccount);
        signInBody.Children().Append(accountRow);
        TextBox cliproxyDir;
        StackPanel dirRow;
        dirRow.Spacing(6);
        dirRow.Children().Append(strongTextBlock(QStringLiteral("Account directory")));
        dirRow.Children().Append(secondaryTextBlock(QStringLiteral(
            "Where CLI Proxy API keeps its account files. Leave empty to detect it automatically.")));
        dirRow.Children().Append(cliproxyDir);
        signInBody.Children().Append(dirRow);
        Border signInCard = win::cardContainer(signInBody);

        // Rebuilding the account list must not read as the user choosing. The
        // guard latches the written index, exactly like programmaticSpeechIndex:
        // WinUI can defer SelectionChanged past a bool that was already cleared.
        auto accountOptions = std::make_shared<QList<RowOption>>();
        const auto populateAccounts = [this, cliproxyAccount, accountOptions](const QString &id) {
            const QString selected = signIn.cliproxyAccount(id);
            *accountOptions = cliproxyAccountOptions(ProviderSignIn::cliproxyAccountType(id),
                                                     selected,
                                                     signIn.resolvedAccountDirectory());
            int selectedIndex = 0;
            for (int index = 0; index < accountOptions->size(); ++index) {
                if (accountOptions->at(index).id == selected) {
                    selectedIndex = index;
                }
            }
            programmaticAccountIndex = selectedIndex;
            cliproxyAccount.Items().Clear();
            for (const RowOption &option : *accountOptions) {
                ComboBoxItem item;
                item.Content(box_value(win::hs(option.label)));
                item.IsEnabled(option.enabled);
                cliproxyAccount.Items().Append(item);
            }
            cliproxyAccount.SelectedIndex(selectedIndex);
        };
        // What a probe verdict may touch: the card's shape for the selected
        // provider, never the account list or the directory text — a probe can
        // land seconds later, mid-typing, and must not erase the edit.
        const auto updateSignInVisibility = [this, signInCard, useCliproxy, accountRow,
                                             dirRow](const QString &id) {
            const bool supported = ProviderSignIn::supportsCliproxy(id);
            signInCard.Visibility(supported ? Visibility::Visible : Visibility::Collapsed);
            if (!supported) {
                return false;
            }
            const bool cliproxy = signIn.usingCliproxy(id);
            useCliproxy.IsChecked(cliproxy);
            accountRow.Visibility(cliproxy ? Visibility::Visible : Visibility::Collapsed);
            dirRow.Visibility(cliproxy ? Visibility::Visible : Visibility::Collapsed);
            return cliproxy;
        };
        const auto refreshSignInCard = [this, updateSignInVisibility, cliproxyDir,
                                        populateAccounts](const QString &id) {
            if (!updateSignInVisibility(id)) {
                return;
            }
            cliproxyDir.Text(win::hs(signIn.configuredAccountDirectory()));
            cliproxyDir.PlaceholderText(win::hs(signIn.resolvedAccountDirectory()));
            populateAccounts(id);
        };

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
                                       accuracy, updateSignInVisibility] {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                status.set(QStringLiteral("No transcription service is available."),
                           StatusTone::Caution);
                return;
            }
            const QString id = options.at(index).first;
            // The Local card explains itself; the generic facts would repeat it.
            showProviderStats(stats, controller->providerRegistry()->speechProviders(),
                              id == kLocal ? QString() : id);
            updateSignInVisibility(id);
            accuracy.Visibility(id == QStringLiteral("codex")
                                    ? Visibility::Visible : Visibility::Collapsed);
            showLocalChoice();
            if (id == kLocal) {
                // Next opens as soon as a download has started: it keeps going
                // while setup continues, and the Ready page shows where it got to.
                const bool downloaded = localSpeech->modelState(localChoice()).downloaded;
                status.set(downloaded ? QString()
                           : localDownloadStarted()
                               ? QStringLiteral("The download keeps going while you finish setup.")
                               : QStringLiteral("Download a model to continue. It keeps going while "
                                                "you finish setup."),
                           StatusTone::Neutral);
                setShown(hint, false);
                setShown(check, false);
                return;
            }
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
        transcriptionRefresh = describeSelected;
        choices.SelectionChanged([this, choices, options, describeSelected,
                                  refreshSignInCard](const auto &, const auto &) {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            if (!wasProgrammatic(programmaticSpeechIndex, index)) {
                speechSelectionSettled = true;
                // An explicit choice here outranks the Welcome path's default.
                welcomeChoice.providerChosen();
            }
            setSpeechProvider(options.at(index).first);
            describeSelected();
            refreshSignInCard(options.at(index).first);
            refreshGates();
        });

        const auto runChecks = [this, choices, options, statuses, describeSelected] {
            const quint64 generation = ++checkGeneration;
            for (int index = 0; index < options.size(); ++index) {
                const QString id = options.at(index).first;
                if (id == kLocal) {
                    continue;
                }
                const StatusCell rowStatus = statuses.at(size_t(index));
                rowStatus.set(QStringLiteral("Checking…"), StatusTone::Neutral);
                probeSpeechProvider(id, generation,
                                    [this, id, rowStatus, choices, options,
                                     describeSelected](const SpeechPrepareResult &result) {
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

        // A sign-in change invalidates only the selected service's verdict, and
        // a probe can be a network OAuth refresh, so only that one re-probes.
        const auto reprobeSelected = [this, choices, options, statuses, describeSelected] {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            const QString id = options.at(index).first;
            const StatusCell rowStatus = statuses.at(size_t(index));
            speechReady.remove(id);
            speechMessage.remove(id);
            rowStatus.set(QStringLiteral("Checking…"), StatusTone::Neutral);
            describeSelected();
            refreshGates();
            const quint64 generation = ++checkGeneration;
            probeSpeechProvider(id, generation,
                                [this, id, rowStatus, describeSelected](
                                    const SpeechPrepareResult &result) {
                rowStatus.set(result.ok ? QStringLiteral("Ready")
                                        : QStringLiteral("Not set up"),
                              result.ok ? StatusTone::Positive : StatusTone::Caution);
                describeSelected();
                refreshGates();
            });
        };
        useCliproxy.Click([this, useCliproxy, choices, options, refreshSignInCard,
                           reprobeSelected](const auto &, const auto &) {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                return;
            }
            const QString id = options.at(index).first;
            signIn.setUseCliproxy(id, useCliproxy.IsChecked().Value());
            refreshSignInCard(id);
            reprobeSelected();
        });
        cliproxyAccount.SelectionChanged([this, cliproxyAccount, accountOptions,
                                          choices, options,
                                          reprobeSelected](const auto &, const auto &) {
            const int selected = cliproxyAccount.SelectedIndex();
            const int index = choices.SelectedIndex();
            if (selected < 0 || selected >= accountOptions->size()
                || index < 0 || index >= options.size()
                || wasProgrammatic(programmaticAccountIndex, selected)) {
                return;
            }
            signIn.setCliproxyAccount(options.at(index).first, accountOptions->at(selected).id);
            reprobeSelected();
        });
        const auto commitDir = [this, cliproxyDir, choices, options, refreshSignInCard,
                                reprobeSelected] {
            const QString directory = win::qs(cliproxyDir.Text()).trimmed();
            if (directory == signIn.configuredAccountDirectory()) {
                return;
            }
            signIn.setAccountDirectory(directory);
            // The Welcome gate reads this too, and its own check will not run
            // again until that page is shown.
            QStringList providerIds;
            for (const auto &option : options) {
                providerIds.append(option.first);
            }
            cliproxyReady = signIn.anyUsableAccount(providerIds);
            const int index = choices.SelectedIndex();
            if (index >= 0 && index < options.size()) {
                refreshSignInCard(options.at(index).first);
            }
            reprobeSelected();
        };
        cliproxyDir.LostFocus([commitDir](const auto &, const auto &) { commitDir(); });
        cliproxyDir.KeyDown([commitDir](const auto &, const Input::KeyRoutedEventArgs &args) {
            if (args.Key() == Windows::System::VirtualKey::Enter) {
                commitDir();
            }
        });

        panel.Children().Append(choices);
        panel.Children().Append(signInCard);
        refreshSignInCard(controller->settings()->speechProvider());
        if (localSpeech) {
            panel.Children().Append(makeLocalSection());
            const auto refresh = [this, describeSelected] {
                describeSelected();
                refreshGates();
            };
            QObject::connect(localSpeech, &LocalSetup::changed, pageScope.get(), refresh);
            QObject::connect(&localSpeech->models(), &LocalModelStore::downloadProgress, pageScope.get(),
                             [this] { showLocalDownload(); });
            localSpeech->probeHardware();
        }
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

    // The refinement twin of autoSelectSpeechProvider, once every provider
    // and the runner check have answered. None, someone's own runner and
    // their own server are choices, never an unready sign-in to move away
    // from; setupRefinementChoice keeps them. Saved before the button is
    // checked, so its Checked handler finds nothing left to write.
    void autoSelectRefinementProvider(const std::vector<RefinementOption> &options)
    {
        LocalSetup *local = controller->localSetup();
        const bool probing = std::any_of(options.cbegin(), options.cend(), [this](const RefinementOption &option) {
            return !refinementReady.contains(option.id);
        });
        if (refinementSelectionSettled || probing || local->detectingRunners()) {
            return;
        }
        refinementSelectionSettled = true;
        const QString saved = controller->settings()->refinementProvider();
        QStringList ready;
        for (const RefinementOption &option : options) {
            if (refinementReady.value(option.id, false)) {
                ready.append(option.id);
            }
        }
        const QString chosen = setupRefinementChoice(saved, ready, local->runnerChoice().available.has_value(),
                                                     controller->settings()->refinementProviderChosen());
        if (chosen == saved) {
            return;
        }
        selectRefinement(chosen);
        for (const RefinementOption &option : options) {
            option.button.IsChecked(option.id == chosen);
        }
    }

    void selectRefinement(const QString &id)
    {
        controller->settings()->setRefinementProvider(id);
        if (id != kNone) {
            lastRefinementProvider = id;
        }
        if (refinementRefresh) {
            refinementRefresh();
        }
    }

    void showRefinement()
    {
        StackPanel panel = page(
            QStringLiteral("Refinement"),
            QStringLiteral("Refinement can clean up a raw transcript after dictation. Choose a provider, or skip cleanup."));
        const QList<ProviderDescriptor> registered = controller->providerRegistry()->refinementProviders();
        const QString saved = controller->settings()->refinementProvider();
        auto options = std::make_shared<std::vector<RefinementOption>>();
        // The cloud providers use a sign-in; a local runner and a custom
        // endpoint are models the person runs. Each group is its own card,
        // and one GroupName makes them one choice.
        const auto addGroup = [&](const QString &title, const QStringList &ids) {
            StackPanel list{nullptr};
            for (const QString &id : ids) {
                const auto found = std::find_if(registered.cbegin(), registered.cend(),
                                                [&id](const ProviderDescriptor &provider) { return provider.id == id; });
                if (found == registered.cend()) {
                    continue;
                }
                if (!list) {
                    list = rowList(card(panel, title));
                }
                const bool ownModel = id == kLocal || id == kEndpoint;
                RefinementOption option{id,
                                        id == kLocal      ? QStringLiteral("This computer")
                                        : id == kEndpoint ? QStringLiteral("A server I run")
                                                          : found->label};
                option.status = statusCell(ownModel ? QString() : QStringLiteral("Checking…"),
                                           StatusTone::Neutral);
                StackPanel text = rowText(strongTextBlock(option.label));
                text.Children().Append(secondaryTextBlock(found->setupHint));
                const FrameworkElement mark = id == kLocal      ? FrameworkElement(glyphMark(kComputerGlyph))
                                              : id == kEndpoint ? FrameworkElement(glyphMark(kServerGlyph))
                                                                : brandMark(id);
                option.button = RadioButton();
                option.button.GroupName(L"refinementProvider");
                option.button.VerticalContentAlignment(VerticalAlignment::Center);
                option.button.HorizontalAlignment(HorizontalAlignment::Stretch);
                option.button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                option.button.Content(cardRow(mark, text, option.status.root));
                AutomationProperties::SetName(option.button, win::hs(option.label));
                option.button.IsChecked(id == saved);
                appendRow(list, option.button);
                options->push_back(option);
            }
        };
        addGroup(QStringLiteral("Uses your sign-in"), {QStringLiteral("anthropic"), QStringLiteral("openai")});
        addGroup(QStringLiteral("Your own models"), {kLocal, kEndpoint});
        // A registry with providers this page does not group still offers them.
        QStringList others;
        for (const ProviderDescriptor &provider : registered) {
            if (!QStringList{QStringLiteral("anthropic"), QStringLiteral("openai"), kLocal, kEndpoint}
                     .contains(provider.id)) {
                others.append(provider.id);
            }
        }
        addGroup(QStringLiteral("Cleanup provider"), others);
        lastRefinementProvider = saved != kNone || options->empty() ? saved : options->front().id;

        // None is not a provider card but a way out of all of them: checking
        // it clears the choice, unchecking it returns to the last provider.
        CheckBox skip = wrappingCheckBox(QStringLiteral("Skip cleanup and deliver the raw transcript"));
        skip.IsChecked(saved == kNone);
        panel.Children().Append(skip);

        // Directly under the cards, where the mockup puts it: the warning
        // belongs to the selection above it, not to the stats below.
        InfoBar warning;
        warning.Severity(InfoBarSeverity::Warning);
        warning.IsClosable(false);
        warning.IsOpen(false);
        panel.Children().Append(warning);
        panel.Children().Append(makeRunnerDetail());
        panel.Children().Append(makeEndpointForm());
        StackPanel stats;
        panel.Children().Append(stats);
        CheckBox fast = wrappingCheckBox(QStringLiteral("Fast mode"));
        panel.Children().Append(fast);

        refinementRefresh = [this, options, skip, fast, stats, warning] {
            const QString id = controller->settings()->refinementProvider();
            const bool ownModel = id == kLocal || id == kEndpoint;
            skip.IsChecked(id == kNone);
            showProviderStats(stats, controller->providerRegistry()->refinementProviders(),
                              ownModel ? QString() : id);
            fast.Visibility(id == QStringLiteral("openai") || id == QStringLiteral("anthropic")
                                ? Visibility::Visible : Visibility::Collapsed);
            if (id == QStringLiteral("openai")) {
                fast.IsChecked(controller->settings()->openAiFastMode());
            } else if (id == QStringLiteral("anthropic")) {
                fast.IsChecked(controller->settings()->anthropicFastMode());
            }
            setShown(runner.root, id == kLocal);
            setShown(endpointForm.root, id == kEndpoint);
            // Refinement stays ungated: an unready provider is a warning, not
            // a wall, because the raw transcript is still delivered. The
            // own-model choices show their own state instead.
            const bool unready = !ownModel && refinementReady.contains(id) && !refinementReady.value(id);
            warning.IsOpen(unready);
            for (const RefinementOption &option : *options) {
                if (unready && option.id == id) {
                    warning.Message(win::hs(
                        QStringLiteral("%1 is not signed in. Dictation will deliver the raw transcript.")
                            .arg(option.label)));
                }
            }
            showRunner(*options);
            showEndpointCheck();
        };
        for (const RefinementOption &option : *options) {
            option.button.Checked([this, id = option.id](const auto &, const auto &) {
                if (controller->settings()->refinementProvider() != id) {
                    selectRefinement(id);
                }
            });
            option.button.Click([this](const auto &, const auto &) { refinementSelectionSettled = true; });
        }
        skip.Click([this, skip, options](const auto &, const auto &) {
            refinementSelectionSettled = true;
            if (skip.IsChecked().Value()) {
                for (const RefinementOption &option : *options) {
                    option.button.IsChecked(false);
                }
                selectRefinement(kNone);
                return;
            }
            for (const RefinementOption &option : *options) {
                if (option.id == lastRefinementProvider) {
                    selectRefinement(option.id);
                    option.button.IsChecked(true);
                }
            }
        });
        fast.Click([this, fast](const auto &, const auto &) {
            const bool checked = fast.IsChecked().Value();
            const QString id = controller->settings()->refinementProvider();
            if (id == QStringLiteral("openai")) {
                controller->settings()->setOpenAiFastMode(checked);
            } else if (id == QStringLiteral("anthropic")) {
                controller->settings()->setAnthropicFastMode(checked);
            }
        });
        LocalSetup *local = controller->localSetup();
        QObject::connect(local, &LocalSetup::changed, pageScope.get(), [this, options] {
            showRunner(*options);
            showEndpointCheck();
            autoSelectRefinementProvider(*options);
        });
        QObject::connect(local, &LocalSetup::pullProgress, pageScope.get(), [this] { showPull(); });
        content.Children().Append(panel);
        refinementRefresh();
        local->probeHardware();
        local->detectRunners();

        const quint64 generation = ++checkGeneration;
        for (const RefinementOption &option : *options) {
            probeRefinementProvider(option.id, generation,
                                    [this, id = option.id, status = option.status, options](bool ok) {
                // The own-model rows say what is on this computer, not a
                // sign-in verdict; showRunner keeps them.
                if (id != kLocal && id != kEndpoint) {
                    status.set(ok ? QStringLiteral("Ready") : QStringLiteral("Not signed in"),
                               ok ? StatusTone::Positive : StatusTone::Caution);
                }
                autoSelectRefinementProvider(*options);
                refinementRefresh();
            });
        }
    }

    // Refinement through a Local Runner: what was found, the model to use,
    // the suggested cleanup model to pull through Ollama, and what to do
    // when nothing runs.
    StackPanel makeRunnerDetail()
    {
        runner.root = StackPanel();
        runner.root.Spacing(8);
        runner.status = textBlock(QString());
        runner.root.Children().Append(runner.status);

        StackPanel body;
        body.Margin({16, 4, 16, 4});
        runner.card = win::cardContainer(body);
        runner.root.Children().Append(runner.card);
        StackPanel modelText = rowText(textBlock(QStringLiteral("Model"), false));
        modelText.Children().Append(secondaryTextBlock(QStringLiteral("A cleanup model the runner has downloaded.")));
        runner.model = ComboBox();
        runner.model.MinWidth(240);
        AutomationProperties::SetName(runner.model, L"Cleanup model");
        runner.modelRow = cardRow(FrameworkElement{nullptr}, modelText, runner.model);
        body.Children().Append(runner.modelRow);
        runner.suggestionSeparator = rowSeparator();
        body.Children().Append(runner.suggestionSeparator);
        runner.suggestionText = textBlock(QString());
        StackPanel pullColumn;
        pullColumn.Spacing(6);
        pullColumn.VerticalAlignment(VerticalAlignment::Center);
        runner.pull = Button();
        runner.pull.Content(box_value(L"Download with Ollama"));
        runner.pull.HorizontalAlignment(HorizontalAlignment::Right);
        pullColumn.Children().Append(runner.pull);
        runner.pullProgress = ProgressBar();
        runner.pullProgress.Width(160);
        runner.pullProgress.Maximum(1000);
        pullColumn.Children().Append(runner.pullProgress);
        runner.suggestion = cardRow(FrameworkElement{nullptr}, rowText(runner.suggestionText), pullColumn);
        body.Children().Append(runner.suggestion);

        runner.noRunner = StackPanel();
        runner.noRunner.Spacing(8);
        runner.noRunner.Children().Append(secondaryTextBlock(QStringLiteral(
            "Cleanup models run in a separate app. Install Ollama, then choose Check again and Speecher "
            "will set up a model through it. LM Studio and llama-server work too.")));
        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        Button getOllama;
        getOllama.Content(box_value(L"Get Ollama"));
        buttons.Children().Append(getOllama);
        Button checkAgain;
        checkAgain.Content(box_value(L"Check again"));
        buttons.Children().Append(checkAgain);
        runner.noRunner.Children().Append(buttons);
        InfoBar rawWarning;
        rawWarning.Severity(InfoBarSeverity::Warning);
        rawWarning.IsClosable(false);
        rawWarning.Message(L"Until a runner is set up, dictation delivers the raw transcript.");
        rawWarning.IsOpen(true);
        runner.noRunner.Children().Append(rawWarning);
        runner.root.Children().Append(runner.noRunner);

        getOllama.Click([](const auto &, const auto &) {
            ShellExecuteW(nullptr, L"open", L"https://ollama.com/download", nullptr, nullptr, SW_SHOWNORMAL);
        });
        checkAgain.Click([this](const auto &, const auto &) { controller->localSetup()->detectRunners(); });
        runner.pull.Click([this](const auto &, const auto &) {
            if (const std::optional<CleanupModel> model = controller->localSetup()->suggestedCleanupModel()) {
                controller->localSetup()->pullCleanupModel(model->ollamaTag);
            }
        });
        // A pick of another model is the person's; showRunner's own selection
        // matches what is saved and writes nothing.
        runner.model.SelectionChanged([this](const winrt::Windows::Foundation::IInspectable &sender, const auto &) {
            const auto item = sender.as<ComboBox>().SelectedItem();
            if (!item) {
                return;
            }
            const RunnerChoice choice = controller->localSetup()->runnerChoice();
            const QString model = win::qs(unbox_value<hstring>(item));
            if (choice.available && choice.selection.model != model) {
                controller->settings()->setLocalRunnerSettings({choice.available->id, model});
            }
        });
        return runner.root;
    }

    void showRunner(const std::vector<RefinementOption> &options)
    {
        if (!runner.root) {
            return;
        }
        LocalSetup *local = controller->localSetup();
        const RunnerChoice choice = local->runnerChoice();
        const bool found = choice.available.has_value();
        const bool detecting = local->detectingRunners();
        for (const RefinementOption &option : options) {
            if (option.id == kLocal) {
                option.status.set(detecting ? QStringLiteral("Checking…")
                                  : found   ? QStringLiteral("%1 found").arg(choice.available->name)
                                            : QStringLiteral("No runner"),
                                  found ? StatusTone::Positive : StatusTone::Neutral);
            }
        }
        runner.status.Text(win::hs(
            detecting ? QStringLiteral("Looking for Ollama, LM Studio and llama-server…")
            : found   ? QStringLiteral("%1 %2 is running on this computer.")
                          .arg(choice.available->name, choice.available->version).simplified()
            : choice.selection.runner.isEmpty()
                ? QStringLiteral("No local runner found on this computer.")
                : QStringLiteral("%1 is unavailable. Your saved selection is unchanged.")
                      .arg(localRunnerName(choice.selection.runner))));
        setShown(runner.noRunner, !found && !detecting);
        setShown(runner.card, found);
        if (!found) {
            return;
        }
        // The runner's models, plus a saved one it does not list, so the
        // saved choice stays on screen rather than silently changing.
        QStringList models = choice.available->models;
        if (!choice.selection.model.isEmpty() && !models.contains(choice.selection.model)) {
            models.append(choice.selection.model);
        }
        if (runner.shownModels != models) {
            runner.shownModels = models;
            runner.model.Items().Clear();
            for (const QString &model : models) {
                runner.model.Items().Append(box_value(win::hs(model)));
            }
        }
        runner.model.SelectedIndex(int(models.indexOf(choice.selection.model)));
        setShown(runner.modelRow, !models.isEmpty());

        const LocalSetup::Pull pull = local->pull();
        const bool offer = choice.showSuggestion || pull.running;
        setShown(runner.suggestion, offer);
        setShown(runner.suggestionSeparator, offer && !models.isEmpty());
        setShown(runner.pull, !pull.running && choice.offerPull);
        showPull();
    }

    // The pull's bytes, many times a second, without the rest of the card.
    void showPull()
    {
        if (!runner.root) {
            return;
        }
        const LocalSetup::Pull pull = controller->localSetup()->pull();
        const std::optional<CleanupModel> suggested = controller->localSetup()->suggestedCleanupModel();
        setShown(runner.pullProgress, pull.running);
        if (pull.running) {
            runner.pullProgress.Value(pull.totalBytes > 0 ? double(pull.completedBytes * 1000 / pull.totalBytes) : 0);
            runner.suggestionText.Text(win::hs(QStringLiteral("Downloading %1 through Ollama: %2 of %3")
                                                   .arg(suggested ? suggested->name : pull.tag,
                                                        downloadSizeText(pull.completedBytes),
                                                        downloadSizeText(pull.totalBytes))));
        } else if (suggested) {
            runner.suggestionText.Text(win::hs(
                QStringLiteral("Suggested for this computer: %1, %2.%3")
                    .arg(suggested->name, downloadSizeText(suggested->sizeBytes),
                         pull.error.isEmpty() ? QString() : QStringLiteral("\n") + pull.error)));
        } else {
            runner.suggestionText.Text(L"Cleanup would be slow on this computer. A cloud provider or "
                                       L"skipping cleanup will feel faster.");
        }
    }

    // The refinement Custom Endpoint: format, server, key, and a model the
    // server lists once connected, or one typed.
    StackPanel makeEndpointForm()
    {
        endpointForm.root = StackPanel();
        endpointForm.root.Spacing(8);
        // With the CLI Proxy API preset, the server and key shown are the proxy's.
        const RefinementEndpoint saved = resolvedRefinementEndpoint(controller->settings()->snapshot().refinement);
        StackPanel rows = rowList(card(endpointForm.root, QString()));
        const auto addRow = [&rows](const QString &label, const QString &help, const FrameworkElement &control) {
            StackPanel text = rowText(textBlock(label, false));
            if (!help.isEmpty()) {
                text.Children().Append(secondaryTextBlock(help));
            }
            AutomationProperties::SetName(control, win::hs(label));
            appendRow(rows, cardRow(FrameworkElement{nullptr}, text, control));
        };
        endpointForm.format = combo({{QStringLiteral("openai"), QStringLiteral("OpenAI-compatible (Chat Completions)")},
                                     {QStringLiteral("anthropic"), QStringLiteral("Anthropic-compatible (Messages)")}},
                                    saved.format);
        addRow(QStringLiteral("Format"), QString(), endpointForm.format);
        endpointForm.url = TextBox();
        endpointForm.url.MinWidth(280);
        endpointForm.url.PlaceholderText(L"http://localhost:8080/v1");
        endpointForm.url.Text(win::hs(saved.apiBase));
        addRow(QStringLiteral("Server URL"), QString(), endpointForm.url);
        endpointForm.key = PasswordBox();
        endpointForm.key.MinWidth(280);
        endpointForm.key.PlaceholderText(L"Optional");
        endpointForm.key.Password(win::hs(saved.apiKey));
        addRow(QStringLiteral("API key"), keyStorageHelp(), endpointForm.key);
        StackPanel modelControls;
        modelControls.Orientation(Orientation::Horizontal);
        modelControls.Spacing(8);
        endpointForm.model = ComboBox();
        endpointForm.model.IsEditable(true);
        endpointForm.model.MinWidth(200);
        // An editable ComboBox drops Text set before it loads, and a save
        // would then clear the model; the saved one goes in as an item.
        if (!saved.model.isEmpty()) {
            endpointForm.model.Items().Append(box_value(win::hs(saved.model)));
            endpointForm.model.SelectedIndex(0);
        }
        AutomationProperties::SetName(endpointForm.model, L"Endpoint model");
        modelControls.Children().Append(endpointForm.model);
        Button connect;
        connect.Content(box_value(L"Connect"));
        modelControls.Children().Append(connect);
        addRow(QStringLiteral("Model"), QStringLiteral("Connect to list the server's models, or type one."),
               modelControls);
        endpointForm.status = textBlock(QString());
        endpointForm.root.Children().Append(endpointForm.status);

        // Each field saves only its own edit, compared with what that field
        // showed: editRefinementEndpoint decides what the edit does to a
        // preset and to the stored key.
        endpointForm.shownUrl = saved.apiBase;
        endpointForm.shownKey = saved.apiKey;
        endpointForm.url.LostFocus([this](const auto &, const auto &) { saveEndpointUrl(); });
        endpointForm.key.LostFocus([this](const auto &, const auto &) { saveEndpointKey(); });
        endpointForm.format.SelectionChanged([this](const auto &, const auto &) {
            saveEndpointEdit({.format = endpointForm.format.SelectedIndex() == 1 ? QStringLiteral("anthropic")
                                                                                 : QStringLiteral("openai")});
        });
        endpointForm.model.LostFocus([this](const auto &, const auto &) { saveEndpointModel(); });
        endpointForm.model.SelectionChanged([this](const auto &, const auto &) { saveEndpointModel(); });
        connect.Click([this](const auto &, const auto &) {
            saveEndpointUrl();
            saveEndpointKey();
            saveEndpointModel();
            controller->localSetup()->checkRefinementEndpoint(controller->settings()->snapshot().refinement);
        });
        return endpointForm.root;
    }

    void saveEndpointEdit(const RefinementEndpointEdit &edit)
    {
        AppSettings settings = controller->settings()->snapshot();
        editRefinementEndpoint(settings, edit);
        controller->settings()->applySnapshot(settings);
        showEndpointCheck();
    }

    void saveEndpointUrl()
    {
        const QString url = win::qs(endpointForm.url.Text());
        if (url != endpointForm.shownUrl) {
            endpointForm.shownUrl = url;
            saveEndpointEdit({.baseUrl = url});
        }
    }

    void saveEndpointKey()
    {
        const QString key = win::qs(endpointForm.key.Password());
        if (key != endpointForm.shownKey) {
            endpointForm.shownKey = key;
            saveEndpointEdit({.apiKey = key});
        }
    }

    void saveEndpointModel()
    {
        // Refilling the list from a check is not the person picking a model.
        if (endpointForm.refilling) {
            return;
        }
        const QString model = endpointModel();
        if (model != controller->settings()->snapshot().refinement.endpoint.model) {
            saveEndpointEdit({.model = model});
        }
    }

    // The model picked from the server's list, or typed.
    QString endpointModel() const
    {
        const auto picked = endpointForm.model.SelectedItem();
        return (picked ? win::qs(unbox_value<hstring>(picked)) : win::qs(endpointForm.model.Text())).trimmed();
    }

    void showEndpointCheck()
    {
        if (!endpointForm.root) {
            return;
        }
        const LiveFacts facts = controller->localSetup()->liveFacts();
        endpointForm.status.Text(win::hs(facts.refinementEndpointStatus));
        setShown(endpointForm.status, !facts.refinementEndpointStatus.isEmpty());
        if (facts.refinementEndpointModels.isEmpty() || endpointForm.shownModels == facts.refinementEndpointModels) {
            return;
        }
        endpointForm.shownModels = facts.refinementEndpointModels;
        // With none typed, LocalSetup has saved the server's first model.
        const QString typed = endpointModel().isEmpty()
            ? controller->settings()->refinementEndpointSettings().model : endpointModel();
        // A typed model the server does not list stays on offer, first.
        QStringList models = facts.refinementEndpointModels;
        if (!typed.isEmpty() && !models.contains(typed)) {
            models.prepend(typed);
        }
        endpointForm.refilling = true;
        endpointForm.model.Items().Clear();
        for (const QString &model : models) {
            endpointForm.model.Items().Append(box_value(win::hs(model)));
        }
        endpointForm.model.SelectedIndex(int(models.indexOf(typed)));
        endpointForm.refilling = false;
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
            return localSpeech
                ? QStringLiteral("No sign-in was found. Sign in, or choose to run on this computer.")
                : QStringLiteral("No ChatGPT, Claude, or CLI Proxy API sign-in was found on this computer.");
        case 1:
            if (localSelected()) {
                return QStringLiteral("Download a speech model to continue.");
            }
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

    // The Local Model the Transcription step chose while it is still
    // downloading, else empty.
    QString downloadingModel() const
    {
        if (!localSelected()) {
            return {};
        }
        const LocalModel &model = localChoice();
        return localSpeech->modelState(model).downloading ? model.id : QString();
    }

    void renderReadyComplete()
    {
        const QString downloading = downloadingModel();
        if (downloading.isEmpty()) {
            readyBody.Children().Append(
                statusCell(QStringLiteral("Setup is complete."), StatusTone::Positive).root);
        } else {
            readyBody.Children().Append(
                textBlock(QStringLiteral("Setup is complete except for the speech model download.")));
            InfoBar notice;
            notice.Severity(InfoBarSeverity::Informational);
            notice.IsClosable(false);
            notice.Message(L"You can close this window. The download keeps going, and Speecher shows a "
                           L"notification when you can start dictating.");
            notice.IsOpen(true);
            readyBody.Children().Append(notice);
        }

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
        if (localSelected()) {
            const QString label = QStringLiteral("Transcription — %1, on this computer")
                                      .arg(localChoice().name);
            if (downloading.isEmpty()) {
                appendRow(rows, readyRow(glyphMark(kComputerGlyph), label, QStringLiteral("Ready"),
                                         StatusTone::Positive));
            } else {
                // The download this step chose is still going: its row
                // carries the progress instead of a verdict.
                StackPanel progress;
                progress.Orientation(Orientation::Horizontal);
                progress.Spacing(8);
                progress.VerticalAlignment(VerticalAlignment::Center);
                readyDownload.modelId = downloading;
                readyDownload.progress = ProgressBar();
                readyDownload.progress.Width(120);
                readyDownload.progress.Maximum(1000);
                readyDownload.progress.VerticalAlignment(VerticalAlignment::Center);
                progress.Children().Append(readyDownload.progress);
                readyDownload.text = secondaryTextBlock(QString());
                readyDownload.text.VerticalAlignment(VerticalAlignment::Center);
                progress.Children().Append(readyDownload.text);
                Button cancel;
                cancel.Content(box_value(L"Cancel"));
                cancel.Click([this, downloading](const auto &, const auto &) {
                    localSpeech->cancelDownload(downloading);
                });
                progress.Children().Append(cancel);
                appendRow(rows, cardRow(glyphMark(kComputerGlyph), rowText(textBlock(label)), progress));
                showReadyDownload();
            }
        } else {
            appendRow(rows, readyRow(brandMark(speechId),
                                     QStringLiteral("Transcription — %1%2")
                                         .arg(providerLabel(
                                                  controller->providerRegistry()->speechProviders(),
                                                  speechId),
                                              signIn.usingCliproxy(speechId)
                                                  ? QStringLiteral(" (CLI Proxy API)")
                                                  : QString()),
                                     QStringLiteral("Ready"), StatusTone::Positive));
        }

        // Refinement is never gated, so this row reports what the refinement
        // page last saw rather than a readiness the Ready page insists on. A
        // provider the walk never reached has no verdict to report.
        const QString refinementId = controller->settings()->refinementProvider();
        QString refinementName = QStringLiteral("None");
        QString refinementStatus = QStringLiteral("No cleanup");
        StatusTone refinementTone = StatusTone::Neutral;
        FrameworkElement refinementMark = glyphMark(L'\uE738');
        if (refinementId != kNone) {
            const AppSettings saved = controller->settings()->snapshot();
            refinementName = refinementId == kLocal
                ? QStringLiteral("%1 with %2").arg(localRunnerName(saved.refinement.localRunner.runner),
                                                   saved.refinement.localRunner.model)
                : refinementId == kEndpoint
                ? QStringLiteral("%1 on your server").arg(resolvedRefinementEndpoint(saved.refinement).model)
                : providerLabel(controller->providerRegistry()->refinementProviders(), refinementId);
            refinementMark = refinementId == kLocal      ? FrameworkElement(glyphMark(kComputerGlyph))
                             : refinementId == kEndpoint ? FrameworkElement(glyphMark(kServerGlyph))
                                                         : brandMark(refinementId);
            // Someone's own runner or server is ready once its form is filled,
            // which the refinement page's probe may have seen before it was.
            const bool ownModel = refinementId == kLocal || refinementId == kEndpoint;
            TranscriptRefiner *refiner = controller->providerRegistry()->refinementProvider(refinementId);
            const std::optional<bool> ready = ownModel && refiner
                ? std::optional<bool>(refiner->prepare(saved.refinement).ok)
                : refinementReady.contains(refinementId) ? std::optional<bool>(refinementReady.value(refinementId))
                                                         : std::nullopt;
            if (!ready) {
                refinementStatus = QStringLiteral("Not checked");
            } else if (*ready) {
                refinementStatus = QStringLiteral("Ready");
                refinementTone = StatusTone::Positive;
            } else {
                refinementStatus = refinementId == kLocal || refinementId == kEndpoint
                    ? QStringLiteral("Not set up")
                    : QStringLiteral("Not signed in");
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

    void showReadyDownload()
    {
        if (!readyDownload.progress) {
            return;
        }
        const auto progress = localSpeech->downloadProgress(readyDownload.modelId);
        if (!progress) {
            return;
        }
        readyDownload.progress.Value(progress->second > 0 ? double(progress->first * 1000 / progress->second) : 0);
        readyDownload.text.Text(win::hs(QStringLiteral("%1 of %2").arg(downloadSizeText(progress->first),
                                                                       downloadSizeText(progress->second))));
    }

    void showReady()
    {
        StackPanel panel = page(QStringLiteral("Ready to dictate"), QString());
        readyBody = StackPanel();
        readyBody.Spacing(12);
        panel.Children().Append(readyBody);
        content.Children().Append(panel);
        renderReady();
        if (localSpeech) {
            // The download finishing changes the intro and the row, and a
            // cancelled one sends the person back to choose again.
            QObject::connect(localSpeech, &LocalSetup::changed, pageScope.get(), [this] {
                refreshGates();
                renderReady();
            });
            QObject::connect(&localSpeech->models(), &LocalModelStore::downloadProgress, pageScope.get(),
                             [this] { showReadyDownload(); });
        }

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
                                [this](const SpeechPrepareResult &) {
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
        controller->completeSetup();
        window.Close();
        // Files held through setup open in a window of their own.
        if (!controller->popupOnly() && !controller->heldFilesOpening()) {
            controller->showSettingsWindow();
        }
    }

    ApplicationController *controller;
    std::function<void()> firstFrame;
    SetupWindow *setup;
    // Only its effectiveTheme is set, for win::themeBrush.
    win::PaneHost themeHost;
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
    // The Ready page's row for a download still going.
    struct ReadyDownload {
        QString modelId;
        ProgressBar progress{nullptr};
        TextBlock text{nullptr};
    } readyDownload;
    // Local speech, where this build runs it; null otherwise, and then the
    // wizard offers no local path.
    LocalSetup *localSpeech = nullptr;
    // The Transcription page's Local card, null while another page is up.
    struct LocalCard {
        StackPanel section{nullptr};
        TextBlock hardware{nullptr};
        TextBlock caption{nullptr};
        TextBlock name{nullptr};
        Border rating{nullptr};
        TextBlock facts{nullptr};
        Button download{nullptr};
        ProgressBar progress{nullptr};
        TextBlock state{nullptr};
        Button cancel{nullptr};
        ListView compare{nullptr};
    } localCard;
    StatusCell localRowStatus;
    std::function<void()> transcriptionRefresh;
    // The Refinement page's own-model details, null while another page is up.
    struct Runner {
        StackPanel root{nullptr};
        TextBlock status{nullptr};
        Border card{nullptr};
        Grid modelRow{nullptr};
        ComboBox model{nullptr};
        QStringList shownModels;
        Border suggestionSeparator{nullptr};
        Grid suggestion{nullptr};
        TextBlock suggestionText{nullptr};
        Button pull{nullptr};
        ProgressBar pullProgress{nullptr};
        StackPanel noRunner{nullptr};
    } runner;
    struct EndpointForm {
        StackPanel root{nullptr};
        ComboBox format{nullptr};
        TextBox url{nullptr};
        PasswordBox key{nullptr};
        ComboBox model{nullptr};
        QStringList shownModels;
        // What the server and key fields last showed or saved.
        QString shownUrl;
        QString shownKey;
        bool refilling = false;
        TextBlock status{nullptr};
    } endpointForm;
    std::function<void()> refinementRefresh;
    // The provider to go back to when Skip cleanup is cleared.
    QString lastRefinementProvider;
    // The Welcome page's path, kept across Back and Next.
    WelcomeChoice welcomeChoice;
    // Owns the Qt connections of the page on screen.
    std::unique_ptr<QObject> pageScope = std::make_unique<QObject>();
    // Discards the results of a check the wizard has moved on from.
    quint64 checkGeneration = 0;
    // What the last probe said about each provider, by id: the welcome and
    // transcription gates read these rather than probing again.
    QHash<QString, bool> speechReady;
    QHash<QString, QString> speechMessage;
    // The newest probe round asked about each provider; see probeSpeechProvider.
    QHash<QString, quint64> speechProbeGeneration;
    QHash<QString, quint64> refinementProbeGeneration;
    // The sign-in decisions shared with the Qt and SwiftUI assistants.
    ProviderSignIn signIn{*controller->settings()};
    // Whether a usable CLI Proxy API account exists, per the last welcome check.
    bool cliproxyReady = false;
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
    int programmaticAccountIndex = -1;
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

bool SetupWindow::isVisible() const
{
    if (!m_native->window) {
        return false;
    }
    HWND handle = nullptr;
    m_native->window.as<::IWindowNative>()->get_WindowHandle(&handle);
    return handle && IsWindowVisible(handle);
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
