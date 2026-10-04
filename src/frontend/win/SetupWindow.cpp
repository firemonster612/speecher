#include "frontend/win/SetupWindow.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PlatformComposition.h"
#include "app/SetupSteps.h"
#include "core/AppSettings.h"
#include "core/EndpointSettings.h"
#include "core/SettingsStore.h"
#include "core/ShortcutBinding.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"
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
// Where each step sits in the walk, which is core's.
int stepIndex(const QString &id)
{
    const QList<SetupStepInfo> steps = setupSteps();
    for (int index = 0; index < steps.size(); ++index) {
        if (steps.at(index).id == id) {
            return index;
        }
    }
    qFatal("the setup steps have no %s step", qPrintable(id));
}
// Functions rather than constants: the steps name a settings page, which
// builds the schema, and that must not happen during static initialisation.
int shortcutPage()
{
    return stepIndex(QStringLiteral("shortcut"));
}
int readyPage()
{
    return stepIndex(QStringLiteral("ready"));
}
// The width a provider row states for itself inside RadioButtons, which lays
// an item out to its content rather than to the list.
constexpr double choiceRowWidth = 560;

const QString kLocal = QStringLiteral("local");
const QString kEndpoint = QStringLiteral("endpoint");
const QString kNone = QStringLiteral("none");
// Segoe Fluent Icons: a desktop PC for this computer, a network for a server
// someone runs.
constexpr wchar_t kComputerGlyph = L'\uE977';
constexpr wchar_t kServerGlyph = L'\uE968';

void setShown(const UIElement &element, bool shown)
{
    element.Visibility(shown ? Visibility::Visible : Visibility::Collapsed);
}

// Where a status sits on the mockup's scale: neutral while a probe runs or
// when a row is merely reporting, positive once a prerequisite holds, caution
// when it does not.
enum class SetupTone { Neutral, Positive, Caution };

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
    text.Style(Application::Current().Resources()
                   .Lookup(box_value(L"CaptionTextBlockStyle"))
                   .as<Style>());
    win::followSecondaryForeground(text);
    return text;
}

// The brush a tone paints with: the theme's success and caution roles, and the
// secondary foreground for a status that is only reporting, such as "Not
// found" or "No cleanup". Red is never used — nothing the wizard reports is
// beyond the user's reach.
Brush toneBrush(SetupTone tone)
{
    const wchar_t *key = L"TextFillColorSecondaryBrush";
    if (tone == SetupTone::Positive) {
        key = L"SystemFillColorSuccessBrush";
    } else if (tone == SetupTone::Caution) {
        key = L"SystemFillColorCautionBrush";
    }
    const auto resources = Application::Current().Resources();
    const auto boxed = box_value(hstring(key));
    return resources.HasKey(boxed) ? resources.Lookup(boxed).as<Brush>() : nullptr;
}

// The Segoe Fluent glyph a tone leads with: a check, a warning triangle, or
// the dash the mockup's "Not found" and "No cleanup" rows carry.
wchar_t toneGlyph(SetupTone tone)
{
    switch (tone) {
    case SetupTone::Positive:
        return L'\uE73E';
    case SetupTone::Caution:
        return L'\uE7BA';
    case SetupTone::Neutral:
        break;
    }
    return L'\uE738';
}

FontIcon toneIcon(SetupTone tone)
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
    void set(const QString &value, SetupTone tone) const
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

StatusCell statusCell(const QString &value, SetupTone tone)
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

// The settings row that holds a refinement provider's Speed choice; empty for
// a provider without one.
QString speedRowFor(const QString &refinementProvider)
{
    if (refinementProvider == QStringLiteral("openai")) {
        return QStringLiteral("openAiSpeed");
    }
    if (refinementProvider == QStringLiteral("anthropic")) {
        return QStringLiteral("anthropicFastMode");
    }
    return {};
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
            value.Style(Application::Current().Resources()
                            .Lookup(box_value(L"CaptionTextBlockStyle"))
                            .as<Style>());
            Grid::SetColumn(value, 1);
            row.Children().Append(value);
            appendRow(panel, row);
        }
        break;
    }
    panel.Visibility(panel.Children().Size() ? Visibility::Visible : Visibility::Collapsed);
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
        paneHost.controller = controller;
        paneHost.alive = alive;
        paneHost.xamlRoot = [this] { return content ? content.XamlRoot() : XamlRoot{nullptr}; };
        paneHost.refresh = [this] { shortcutChanged(); };
        microphone = controller->platform()->createAudioInput(controller->settings(), q);
        QObject::connect(microphone, &AudioInput::levelChanged, q, [this](float value) {
            if (microphoneLevel) {
                microphoneLevel.Value(std::clamp(value, 0.0f, 1.0f));
            }
            if (value <= 0.01f) {
                return;
            }
            if (microphoneStatus) {
                microphoneStatus.Text(win::hs(setupText(SetupText::InputDetected)));
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
        *alive = false;
        microphone->stop();
        if (window) {
            tearingDown = true;
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
            // A Global Shortcut dialog left open gives the hotkey back.
            win::ShortcutRecorder::setRecording(paneHost, false);

            window = nullptr;
            content = nullptr;
            skip = back = next = nullptr;
            if (!tearingDown) {
                controller->setupAssistantClosed();
            }
        });

        Grid root;
        root.RequestedTheme(win::requestedTheme(controller->settings()->theme()));
        // Rating badges pick their brushes by this window's theme.
        paneHost.effectiveTheme = [root] { return root.ActualTheme(); };
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
        titleBar.Title(win::hs(setupWindowTitle()));
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
        skip.Content(box_value(win::hs(setupText(SetupText::SkipSetup))));
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
            } else if (pageIndex == readyPage()) {
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
        SetWindowTextW(handle, setupWindowTitle().toStdWString().c_str());
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
        registerShortcut();
        showPage(singlePage ? shortcutPage() : 0);
        window.Activate();
        HWND handle = nullptr;
        window.as<::IWindowNative>()->get_WindowHandle(&handle);
        AllowSetForegroundWindow(ASFW_ANY);
        SetForegroundWindow(handle);
    }

    // A page's prerequisite, as the wizard currently knows it. Only
    // Transcription, Microphone and the Global Shortcut ask for anything.
    bool gateSatisfied(int index) const
    {
        const QString id = setupSteps().at(index).id;
        if (id == QStringLiteral("transcription")) {
            if (!speechDeadEnd().isEmpty()) {
                return false;
            }
            // A Local Model counts once its download has started: it keeps
            // going while setup continues.
            if (localSelected()) {
                return localDownloadStarted();
            }
            return speechReady.value(controller->settings()->speechProvider(), false);
        }
        if (id == QStringLiteral("microphone")) {
            return microphoneDetected;
        }
        if (id == QStringLiteral("shortcut")) {
            return shortcutRegistered;
        }
        return true;
    }

    // The first page whose gate is unmet, or -1 while every gate holds.
    int firstUnsatisfiedPage() const
    {
        for (int index = 0; index < readyPage(); ++index) {
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
        const bool ready = pageIndex == readyPage() ? firstUnsatisfiedPage() < 0
                                                  : gateSatisfied(pageIndex);
        next.IsEnabled(singlePage || ready);
        const bool offerSkip = !singlePage && pageIndex != readyPage() && firstUnsatisfiedPage() < 0;
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
        speechMessage.insert(id, result.ok ? setupProviderReady(label)
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
            land({false, setupTranscriptionBlocked(false, QString())});
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
        content.Children().Clear();
        const QString id = setupSteps().at(index).id;
        if (id == QStringLiteral("welcome")) showWelcome();
        else if (id == QStringLiteral("transcription")) showTranscription();
        else if (id == QStringLiteral("microphone")) showMicrophone();
        else if (id == QStringLiteral("refinement")) showRefinement();
        else if (id == QStringLiteral("shortcut")) showShortcut();
        else showReady();
        back.Visibility(singlePage || index == 0 ? Visibility::Collapsed : Visibility::Visible);
        next.Content(box_value(singlePage ? L"Done"
                                          : (index == readyPage() ? L"Finish" : L"Next")));
        refreshGates();
    }

    // The mockup's page frame: the title with "Step N of 9" grey on the right,
    // then the page's lead paragraph. The single-page shortcut recorder is not
    // a step in a walk, so it carries no counter.
    StackPanel page(const QString &stepId)
    {
        const SetupStepInfo &step = *findSetupStep(stepId);
        return page(step.title, step.intro);
    }

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
                setupStepCounter(pageIndex + 1, SetupWindow::pageTitles().size()));
            step.VerticalAlignment(VerticalAlignment::Bottom);
            Grid::SetColumn(step, 1);
            header.Children().Append(step);
        }
        column.Children().Append(header);
        if (!body.isEmpty()) {
            TextBlock description = textBlock(body);
            win::followSecondaryForeground(description);
            column.Children().Append(description);
        }
        return column;
    }

    // Every speech provider this assistant sets.
    void setSpeechProvider(const QString &id)
    {
        controller->settings()->setSpeechProvider(id);
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
        content.Children().Append(page(QStringLiteral("welcome")));
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
        if (speechSelectionSettled) {
            return;
        }
        // Every sign-in answers first: moving to this computer on the first
        // verdict would leave a sign-in that answers later unused.
        for (const auto &option : options) {
            if (option.first != kLocal && !speechReady.contains(option.first)) {
                return;
            }
        }
        const QString saved = controller->settings()->speechProvider();
        QStringList ready;
        for (const auto &option : options) {
            if (speechReady.value(option.first, false)) {
                ready.append(option.first);
            }
        }
        QStringList signIns;
        for (const auto &option : options) {
            if (isSetupSignInProvider(option.first)) {
                signIns.append(option.first);
            }
        }
        const QString chosen = setupSpeechChoice(saved, ready,
                                                 localSpeech && localSpeech->canRunAnyModel(),
                                                 signIn.anyUsableAccount(signIns), false);
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

    // The Transcription step's dead-end note, or empty while anything can
    // still transcribe. An unanswered sign-in check is not a missing sign-in,
    // and canRunAnyModel stays optimistic until the hardware probe answers.
    QString speechDeadEnd() const
    {
        QStringList signIns;
        bool signInFound = false;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->speechProviders()) {
            if (isSetupSignInProvider(provider.id)) {
                signIns.append(provider.id);
                signInFound = signInFound || speechReady.value(provider.id, true);
            }
        }
        return setupTranscriptionDeadEnd(signInFound || signIn.anyUsableAccount(signIns),
                                         localSpeech && localSpeech->canRunAnyModel(),
                                         controller->settings()->speechProvider() == kEndpoint,
                                         !signIns.isEmpty());
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
        const QStringList titles = compareTableHeaders();
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
            Grid rating = win::ratingBadge(model.rating, paneHost);
            Grid::SetRow(rating, 1);
            row.Children().Append(rating);
            AutomationProperties::SetName(row, win::hs(model.name));
            card.compare.Items().Append(row);
        }
        table.Children().Append(card.compare);
        table.Children().Append(secondaryTextBlock(localModelText(LocalModelText::CompareNote)));
        Expander compare;
        compare.Header(box_value(win::hs(compareModelsCaption(int(localModelCatalog().size()) - 1))));
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
        card.caption.Text(win::hs(
            localModelText(state.suggested ? LocalModelText::Suggested : LocalModelText::YourChoice)));
        card.name.Text(win::hs(model.name));
        card.rating.Child(win::ratingBadge(model.rating, paneHost));
        card.facts.Text(win::hs(state.cardFacts));
        card.download.IsEnabled(!state.tooLarge);
        card.download.Content(box_value(win::hs(state.tooLarge ? localModelText(LocalModelText::TooLarge)
                                                               : downloadCaption(model.sizeBytes))));

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
                               downloaded ? SetupTone::Positive : SetupTone::Neutral);
        }
    }

    void showTranscription()
    {
        StackPanel panel = page(QStringLiteral("transcription"));
        // Core words the dead end and decides when it shows.
        InfoBar deadEnd;
        deadEnd.Severity(InfoBarSeverity::Warning);
        deadEnd.IsClosable(false);
        deadEnd.IsOpen(false);
        panel.Children().Append(deadEnd);
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
                                                 SetupTone::Neutral);
            StackPanel text = rowText(strongTextBlock(options.at(index).second));
            if (local) {
                text.Children().Append(secondaryTextBlock(
                    setupText(SetupText::LocalSpeechNote)));
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
        const SettingsRow &accuracyRow = setupSchemaRow(QStringLiteral("codexFinalRetranscribe"));
        CheckBox accuracyBox = wrappingCheckBox(accuracyRow.help);
        accuracyBox.IsChecked(controller->settings()->codexFinalRetranscribe());
        StackPanel accuracy = settingRow(accuracyRow.label, accuracyBox);
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
        StackPanel accountRow = settingRow(setupText(SetupText::CliproxyAccount), cliproxyAccount);
        signInBody.Children().Append(accountRow);
        TextBox cliproxyDir;
        StackPanel dirRow;
        dirRow.Spacing(6);
        const SettingsRow &directoryRow = setupSchemaRow(QStringLiteral("cliproxyOauthDir"));
        dirRow.Children().Append(strongTextBlock(directoryRow.label));
        dirRow.Children().Append(secondaryTextBlock(directoryRow.help));
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
        const StatusCell status = statusCell(QString(), SetupTone::Neutral);
        status.root.VerticalAlignment(VerticalAlignment::Top);
        TextBlock hint = secondaryTextBlock(QString());
        Button check;
        check.Content(box_value(win::hs(setupText(SetupText::CheckAgain))));
        check.VerticalAlignment(VerticalAlignment::Top);

        // The credential hint and Check again belong to a service that is not
        // signed in; a ready one needs neither.
        const auto describeProvider = [this, choices, options, stats, status, hint, check,
                                       accuracy, updateSignInVisibility] {
            const int index = choices.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                status.set(setupTranscriptionBlocked(false, QString()), SetupTone::Caution);
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
                               ? setupText(SetupText::DownloadContinues)
                               : setupText(SetupText::DownloadToContinue),
                           SetupTone::Neutral);
                setShown(hint, false);
                setShown(check, false);
                return;
            }
            const bool ready = speechReady.value(id, false);
            const bool checked = speechReady.contains(id);
            status.set(speechMessage.value(id, QStringLiteral("Checking…")),
                       !checked ? SetupTone::Neutral
                                : (ready ? SetupTone::Positive : SetupTone::Caution));
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
        const auto describeSelected = [this, describeProvider, deadEnd, status] {
            describeProvider();
            const QString note = speechDeadEnd();
            deadEnd.Message(win::hs(note));
            deadEnd.IsOpen(!note.isEmpty());
            // The note is the verdict; the status line would report the same
            // missing sign-in a second time.
            if (!note.isEmpty()) {
                setShown(status.root, false);
            }
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
                rowStatus.set(QStringLiteral("Checking…"), SetupTone::Neutral);
                probeSpeechProvider(id, generation,
                                    [this, id, rowStatus, choices, options,
                                     describeSelected](const SpeechPrepareResult &result) {
                    rowStatus.set(setupProviderVerdict(id, result.ok),
                                  result.ok ? SetupTone::Positive : SetupTone::Caution);
                    autoSelectSpeechProvider(choices, options);
                    describeSelected();
                    refreshGates();
                });
            }
        };
        check.Click([runChecks](const auto &, const auto &) { runChecks(); });
        accuracyBox.Click([this, accuracyBox](const auto &, const auto &) {
            controller->settings()->setCodexFinalRetranscribe(accuracyBox.IsChecked().Value());
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
            rowStatus.set(QStringLiteral("Checking…"), SetupTone::Neutral);
            describeSelected();
            refreshGates();
            const quint64 generation = ++checkGeneration;
            probeSpeechProvider(id, generation,
                                [this, id, rowStatus, describeSelected](
                                    const SpeechPrepareResult &result) {
                rowStatus.set(setupProviderVerdict(id, result.ok),
                              result.ok ? SetupTone::Positive : SetupTone::Caution);
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
        StackPanel panel = page(QStringLiteral("microphone"));
        const QList<AudioInputDeviceInfo> devices = controller->platform()->availableAudioInputDevices();
        QList<QPair<QString, QString>> options{{QString(), audioDeviceDefaultLabel()}};
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
        microphoneStatus = textBlock(setupText(SetupText::ListeningForInput));
        microphoneProblem = InfoBar();
        microphoneProblem.Title(L"Check microphone privacy");
        microphoneProblem.Message(L"Allow desktop apps to use the microphone.");
        microphoneProblem.Severity(InfoBarSeverity::Warning);
        microphoneProblem.IsClosable(true);
        microphoneProblem.IsOpen(false);
        Button openSettings;
        openSettings.Content(box_value(win::hs(popupErrorActionLabel({ErrorFix::MicrophonePermission}))));
        openSettings.Click([](const auto &, const auto &) { win::openMicrophonePrivacySettings(); });
        microphoneProblem.ActionButton(openSettings);
        panel.Children().Append(settingRow(setupSchemaRow(QStringLiteral("audioDevice")).label, device));
        panel.Children().Append(settingRow(inputLevelLabel(), microphoneLevel));
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
        const quint64 generation = ++microphoneGeneration;
        QString error;
        if (!microphone->start(&error)) {
            microphoneStatus.Text(hstring(error.toStdWString()));
            microphoneProblem.IsOpen(true);
            return;
        }
        // The same nudge the other assistants give a meter that stays flat.
        QTimer::singleShot(kSetupSilentMicrophoneMs, pageScope.get(), [this, generation] {
            if (generation == microphoneGeneration && !microphoneDetected && microphoneStatus) {
                microphoneStatus.Text(win::hs(setupSilentMicrophoneHint()));
            }
        });
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
        StackPanel panel = page(QStringLiteral("refinement"));
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
                RefinementOption option{id, found->label};
                option.status = statusCell(ownModel ? QString() : QStringLiteral("Checking…"),
                                           SetupTone::Neutral);
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
        addGroup(setupText(SetupText::UsesYourSignIn), {QStringLiteral("anthropic"), QStringLiteral("openai")});
        addGroup(setupText(SetupText::YourOwnModels), {kLocal, kEndpoint});
        // A registry with providers this page does not group still offers them.
        QStringList others;
        for (const ProviderDescriptor &provider : registered) {
            if (!QStringList{QStringLiteral("anthropic"), QStringLiteral("openai"), kLocal, kEndpoint}
                     .contains(provider.id)) {
                others.append(provider.id);
            }
        }
        addGroup(setupText(SetupText::CleanupProvider), others);
        lastRefinementProvider = saved != kNone || options->empty() ? saved : options->front().id;

        // None is not a provider card but a way out of all of them: checking
        // it clears the choice, unchecking it returns to the last provider.
        CheckBox skip = wrappingCheckBox(setupText(SetupText::SkipCleanup));
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
        // The provider's Speed settings row as a choice: OpenAI's Standard,
        // Fast or Ultrafast, Anthropic's Standard or Fast.
        ComboBox speed;
        speed.MinWidth(240);
        TextBlock speedHelp = secondaryTextBlock(QString());
        StackPanel speedRow = settingRow(setupSchemaRow(QStringLiteral("openAiSpeed")).label, speed);
        speedRow.Children().Append(speedHelp);
        panel.Children().Append(speedRow);

        refinementRefresh = [this, options, skip, speed, speedHelp, speedRow, stats, warning] {
            const QString id = controller->settings()->refinementProvider();
            const bool ownModel = id == kLocal || id == kEndpoint;
            skip.IsChecked(id == kNone);
            showProviderStats(stats, controller->providerRegistry()->refinementProviders(),
                              ownModel ? QString() : id);
            const QString speedRowId = speedRowFor(id);
            speedRow.Visibility(speedRowId.isEmpty() ? Visibility::Collapsed : Visibility::Visible);
            if (!speedRowId.isEmpty()) {
                const SettingsRow &speedSetting = setupSchemaRow(speedRowId);
                const AppSettings settings = controller->settings()->snapshot();
                const QString current = speedSetting.value(settings).toString();
                ToolTipService::SetToolTip(speed, box_value(win::hs(speedSetting.tooltip)));
                speedHelp.Text(win::hs(speedSetting.help));
                speed.Items().Clear();
                int selected = -1;
                for (const RowOption &option : speedSetting.options(settings)) {
                    ComboBoxItem item;
                    item.Content(box_value(win::hs(option.label)));
                    item.Tag(box_value(win::hs(option.id)));
                    item.IsEnabled(option.enabled);
                    if (!option.help.isEmpty()) {
                        ToolTipService::SetToolTip(item, box_value(win::hs(option.help)));
                    }
                    if (option.id == current) {
                        selected = int(speed.Items().Size());
                    }
                    speed.Items().Append(item);
                }
                speed.SelectedIndex(selected);
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
                        setupRefinementNotSignedIn(option.label)));
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
        // Refilling the list selects the stored speed again, which WinUI can
        // report late; comparing with the stored speed keeps that from saving.
        // The choice goes through the schema row, which knows how each
        // provider stores it.
        speed.SelectionChanged([this, speed](const auto &, const auto &) {
            const auto item = speed.SelectedItem();
            const QString speedRowId = speedRowFor(controller->settings()->refinementProvider());
            if (!item || speedRowId.isEmpty()) {
                return;
            }
            const QString chosen = win::qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag()));
            AppSettings settings = controller->settings()->snapshot();
            setupSchemaRow(speedRowId).apply(settings, chosen);
            if (settings.refinement.openAiSpeed != controller->settings()->openAiSpeed()) {
                controller->settings()->setOpenAiSpeed(settings.refinement.openAiSpeed);
            }
            if (settings.refinement.anthropicFastMode != controller->settings()->anthropicFastMode()) {
                controller->settings()->setAnthropicFastMode(settings.refinement.anthropicFastMode);
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
                    status.set(setupProviderVerdict(id, ok),
                               ok ? SetupTone::Positive : SetupTone::Caution);
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
        const SettingsRow &runnerModel = setupSchemaRow(QStringLiteral("localRunnerModel"));
        StackPanel modelText = rowText(textBlock(runnerModel.label, false));
        modelText.Children().Append(secondaryTextBlock(runnerModel.help));
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
        runner.pull.Content(box_value(win::hs(setupText(SetupText::DownloadWithOllama))));
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
        runner.noRunner.Children().Append(secondaryTextBlock(setupText(SetupText::InstallRunner)));
        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        Button getOllama;
        getOllama.Content(box_value(win::hs(setupText(SetupText::GetOllama))));
        buttons.Children().Append(getOllama);
        Button checkAgain;
        checkAgain.Content(box_value(win::hs(setupText(SetupText::CheckAgain))));
        buttons.Children().Append(checkAgain);
        runner.noRunner.Children().Append(buttons);
        InfoBar rawWarning;
        rawWarning.Severity(InfoBarSeverity::Warning);
        rawWarning.IsClosable(false);
        rawWarning.Message(win::hs(setupText(SetupText::RawUntilRunner)));
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
                                            : setupText(SetupText::NoRunner),
                                  found ? SetupTone::Positive : SetupTone::Neutral);
            }
        }
        runner.status.Text(win::hs(
            detecting ? setupText(SetupText::LookingForRunners)
            : found   ? QStringLiteral("%1 %2 is running on this computer.")
                          .arg(choice.available->name, choice.available->version).simplified()
            : choice.selection.runner.isEmpty()
                ? setupText(SetupText::NoRunnerFound)
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
        QList<QPair<QString, QString>> formats;
        for (const RowOption &option :
             setupSchemaRow(QStringLiteral("refinementEndpointFormat")).options(AppSettings())) {
            formats.append({option.id, option.label});
        }
        endpointForm.format = combo(formats, saved.format);
        addRow(setupSchemaRow(QStringLiteral("refinementEndpointFormat")).label, QString(), endpointForm.format);
        endpointForm.url = TextBox();
        endpointForm.url.MinWidth(280);
        endpointForm.url.PlaceholderText(L"http://localhost:8080/v1");
        endpointForm.url.Text(win::hs(saved.apiBase));
        addRow(setupSchemaRow(QStringLiteral("refinementEndpointUrl")).label, QString(), endpointForm.url);
        endpointForm.key = PasswordBox();
        endpointForm.key.MinWidth(280);
        endpointForm.key.PlaceholderText(L"Optional");
        endpointForm.key.Password(win::hs(saved.apiKey));
        addRow(setupSchemaRow(QStringLiteral("refinementEndpointApiKey")).label, keyStorageHelp(), endpointForm.key);
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
        addRow(setupSchemaRow(QStringLiteral("refinementEndpointModel")).label, setupText(SetupText::EndpointModelHint),
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

    void showShortcut()
    {
        StackPanel panel = page(QStringLiteral("shortcut"));
        // The Settings window's own Global Shortcut row, so both record in the
        // same dialog. A registration Windows refused says so under it.
        const SettingsRow &shortcutRow = setupSchemaRow(QStringLiteral("globalShortcut"));
        win::RowSnapshot row;
        row.id = shortcutRow.id;
        row.label = shortcutRow.label;
        row.help = shortcutRow.help;
        paneHost.shortcutProblem = shortcutProblem;
        panel.Children().Append(win::cardContainer(win::ShortcutRecorder::element(row, paneHost)));

        // The shortcut and its behaviour are set together; the combo shares
        // the shortcuts/activationMode setting the Dictation page's schema row
        // edits rather than keeping a second copy of the value, and reads
        // each mode's name and help from that row.
        const SettingsRow &modeRow = setupSchemaRow(QStringLiteral("activationMode"));
        QList<QPair<QString, QString>> modes;
        for (const RowOption &option : modeRow.options(controller->settings()->snapshot())) {
            modes.append({option.id, QStringLiteral("%1 — %2").arg(option.label, option.help)});
        }
        ComboBox mode = combo(modes,
                              shortcutActivationModeName(
                                  controller->settings()->shortcutActivationMode()));
        mode.SelectionChanged([this, mode, modes](const auto &, const auto &) {
            controller->settings()->setShortcutActivationMode(
                shortcutActivationModeFromName(modes.at(mode.SelectedIndex()).first));
        });
        panel.Children().Append(settingRow(modeRow.label, mode));
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
        return audioDeviceDefaultLabel();
    }

    // Why a gated page is still unfinished, in one line, for the Ready page's
    // blocked checklist.
    QString gateReason(int index) const
    {
        const SetupStepInfo &step = setupSteps().at(index);
        if (step.id == QStringLiteral("transcription")) {
            if (const QString note = speechDeadEnd(); !note.isEmpty()) {
                return note;
            }
            return setupTranscriptionBlocked(
                localSelected(),
                providerLabel(controller->providerRegistry()->speechProviders(),
                              controller->settings()->speechProvider()));
        }
        if (step.id == QStringLiteral("microphone")) {
            return setupMicrophoneBlocked(SetupMicrophoneProblem::Silent);
        }
        if (step.id == QStringLiteral("shortcut") && !shortcutProblem.isEmpty()) {
            return shortcutProblem;
        }
        return step.blocked;
    }

    Grid readyRow(const FrameworkElement &mark, const QString &label, const QString &status,
                  SetupTone tone)
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
        readyBody.Children().Append(textBlock(setupReadyIntro(true, false)));
        readyBody.Children().Append(strongTextBlock(setupBlockedHeading()));
        StackPanel rows = rowList(card(readyBody, QString()));
        for (int index = 0; index < readyPage(); ++index) {
            if (gateSatisfied(index)) {
                continue;
            }
            StackPanel text = rowText(strongTextBlock(SetupWindow::pageTitles().at(index)));
            text.Children().Append(secondaryTextBlock(gateReason(index)));
            Button go;
            go.Content(box_value(win::hs(setupText(SetupText::GoToStep))));
            go.VerticalAlignment(VerticalAlignment::Center);
            go.Click([this, index](const auto &, const auto &) { showPage(index); });
            appendRow(rows, cardRow(toneIcon(SetupTone::Caution), text, go));
        }
        readyBody.Children().Append(secondaryTextBlock(setupBlockedFooter()));
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
                statusCell(setupReadyIntro(false, false), SetupTone::Positive).root);
        } else {
            readyBody.Children().Append(textBlock(setupReadyIntro(false, true)));
            InfoBar notice;
            notice.Severity(InfoBarSeverity::Informational);
            notice.IsClosable(false);
            notice.Message(win::hs(setupText(SetupText::CloseWhileDownloading)));
            notice.IsOpen(true);
            readyBody.Children().Append(notice);
        }

        // The actual binding, not a hardcoded default: the shortcut step may
        // have recorded anything, a single key included.
        const QString display = controller->globalShortcutDisplay();
        StackPanel how = card(readyBody, setupText(SetupText::HowToDictate));
        how.Children().Append(textBlock(
            setupActivationInstruction(controller->settings()->shortcutActivationMode(), display)));
        how.Children().Append(secondaryTextBlock(QStringLiteral(
            "Speecher stays in the notification area. Open its microphone icon for status, your latest transcript, and settings.")));

        StackPanel rows = rowList(card(readyBody, QString()));
        const QString speechId = controller->settings()->speechProvider();
        if (localSelected()) {
            const QString label = setupChecklistLine(
                QStringLiteral("transcription"), setupLocalSpeechChoice(localChoice().name));
            if (downloading.isEmpty()) {
                appendRow(rows, readyRow(glyphMark(kComputerGlyph), label, QStringLiteral("Ready"),
                                         SetupTone::Positive));
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
            const QString label = providerLabel(controller->providerRegistry()->speechProviders(), speechId);
            appendRow(rows, readyRow(brandMark(speechId),
                                     setupChecklistLine(QStringLiteral("transcription"),
                                                        signIn.usingCliproxy(speechId)
                                                            ? setupCliproxySpeechChoice(label)
                                                            : label),
                                     QStringLiteral("Ready"), SetupTone::Positive));
        }
        appendRow(rows, readyRow(glyphMark(L'\uE720'),
                                 setupChecklistLine(QStringLiteral("microphone"), microphoneLabel()),
                                 QStringLiteral("Ready"), SetupTone::Positive));

        // Refinement is never gated, so this row reports what the refinement
        // page last saw rather than a readiness the Ready page insists on. A
        // provider the walk never reached has no verdict to report.
        const QString refinementId = controller->settings()->refinementProvider();
        QString refinementName = QStringLiteral("None");
        std::optional<bool> refinementIsReady;
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
            refinementIsReady = ownModel && refiner
                ? std::optional<bool>(refiner->prepare(saved.refinement).ok)
                : refinementReady.contains(refinementId) ? std::optional<bool>(refinementReady.value(refinementId))
                                                         : std::nullopt;
        }
        const SetupTone refinementTone = refinementId == kNone || !refinementIsReady ? SetupTone::Neutral
                                          : *refinementIsReady                        ? SetupTone::Positive
                                                                                      : SetupTone::Caution;
        appendRow(rows, readyRow(refinementMark,
                                 setupChecklistLine(QStringLiteral("refinement"), refinementName),
                                 setupRefinementStatus(refinementId, refinementIsReady), refinementTone));
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
        StackPanel panel = page(QStringLiteral("ready"));
        readyBody = StackPanel();
        readyBody.Spacing(12);
        panel.Children().Append(readyBody);
        // Applied when setup finishes, so a skip leaves it alone.
        CheckBox launch;
        launch.Content(box_value(win::hs(setupSchemaRow(QStringLiteral("launchAtLogin")).label)));
        launch.IsChecked(launchAtLogin);
        launch.Click([this, launch](const auto &, const auto &) {
            launchAtLogin = launch.IsChecked().Value();
        });
        panel.Children().Append(launch);
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

        // Finishing must not trust what the transcription page saw however
        // long ago: a sign-in can expire while the wizard sits on a later
        // page. The gate reads speechReady, and refreshGates re-reads it as
        // each verdict lands.
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

    // Registers the saved shortcut, or the default on a first run, and says
    // why when Windows refuses: another app owning the combination is the
    // common case. The shortcut step's gate is this registration holding.
    void registerShortcut()
    {
        const ShortcutBinding saved = controller->globalShortcut();
        const ShortcutBinding effective = saved.isEmpty()
            ? ShortcutBinding(WinGlobalShortcutBinder::defaultShortcut())
            : saved;
        QString error;
        shortcutRegistered = controller->setGlobalShortcut(effective, &error);
        // Only a conflict is answered by recording something else. A key
        // Windows cannot register at all needs its own reason said out loud,
        // or the user retypes the same chord forever.
        shortcutProblem = shortcutRegistered ? QString()
            : WinGlobalShortcutBinder::describesConflict(error) || error.isEmpty()
            ? QStringLiteral("Another app is using %1. Record a different shortcut.")
                  .arg(effective.displayText())
            : error;
        refreshGates();
    }

    void markShortcutRegistered()
    {
        shortcutRegistered = true;
        shortcutProblem.clear();
        refreshGates();
    }

    // The Global Shortcut row recorded or reset a binding. A refusal leaves
    // the gate as it was: the binding before it may still hold.
    void shortcutChanged()
    {
        if (paneHost.shortcutProblem.isEmpty()) {
            markShortcutRegistered();
        } else {
            shortcutProblem = paneHost.shortcutProblem;
        }
        if (window && pageIndex == shortcutPage()) {
            showPage(pageIndex);
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
        // Always register again, not only when nothing is saved: a
        // combination another app took while the assistant was open is stored
        // happily and does nothing. A setup without a working shortcut leaves
        // nothing to dictate with, so skipping holds here too.
        registerShortcut();
        if (!shortcutRegistered) {
            showPage(shortcutPage());
            return;
        }
        controller->settings()->setLaunchAtLogin(launchAtLogin);
        controller->completeSetup();
        window.Close();
    }

    ApplicationController *controller;
    std::function<void()> firstFrame;
    SetupWindow *setup;
    // What the rating badges and the Global Shortcut row need from a window.
    win::PaneHost paneHost;
    // The window's lifetime token, which the shortcut dialog's handler checks.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    Window window{nullptr};
    StackPanel content{nullptr};
    Button skip{nullptr};
    Button back{nullptr};
    Button next{nullptr};
    AudioInput *microphone = nullptr;
    ProgressBar microphoneLevel{nullptr};
    TextBlock microphoneStatus{nullptr};
    InfoBar microphoneProblem{nullptr};
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
    // Owns the Qt connections of the page on screen.
    std::unique_ptr<QObject> pageScope = std::make_unique<QObject>();
    // Discards the results of a check the wizard has moved on from.
    quint64 checkGeneration = 0;
    // What the last probe said about each provider, by id: the transcription
    // gate reads these rather than probing again.
    QHash<QString, bool> speechReady;
    QHash<QString, QString> speechMessage;
    // The newest probe round asked about each provider; see probeSpeechProvider.
    QHash<QString, quint64> speechProbeGeneration;
    QHash<QString, quint64> refinementProbeGeneration;
    // The sign-in decisions shared with the Qt and SwiftUI assistants.
    ProviderSignIn signIn{*controller->settings()};
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
    // Retires a silent-meter hint when the meter restarts.
    quint64 microphoneGeneration = 0;
    // The shortcut step's gate: the binding registered with Windows, and why
    // it did not when it did not.
    bool shortcutRegistered = false;
    QString shortcutProblem;
    // Set while the front end closes the window on its way out.
    bool tearingDown = false;
    int pageIndex = 0;
    bool launchAtLogin;
    bool singlePage = false;
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
    QStringList titles;
    for (const SetupStepInfo &step : setupSteps()) {
        titles.append(step.title);
    }
    return titles;
}

void SetupWindow::skipForTest()
{
    m_native->complete(true);
}

QString SetupWindow::currentPageTitleForTest() const
{
    return pageTitles().at(m_native->pageIndex);
}

void SetupWindow::showPageForTest(const QString &stepId)
{
    m_native->showPage(stepIndex(stepId));
}

bool SetupWindow::finishEnabledForTest() const
{
    return m_native->next && m_native->next.IsEnabled();
}

bool SetupWindow::captureForTest(const QString &path)
{
    HWND handle = nullptr;
    m_native->window.as<::IWindowNative>()->get_WindowHandle(&handle);
    return win::printWindowTo(handle, path);
}

QStringList SetupWindow::welcomeCopyForTest()
{
    return {findSetupStep(QStringLiteral("welcome"))->intro};
}

} // namespace speecher
