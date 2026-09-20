#include "frontend/win/SetupWindow.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/AppSettings.h"
#include "core/OutputFormat.h"
#include "core/SettingsStore.h"
#include "core/ShortcutBinding.h"
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
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

#include <QDebug>
#include <QKeySequence>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace speecher {
namespace {

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

constexpr int setupWidth = 760;
constexpr int setupHeight = 560;
constexpr int shortcutPage = 6;

TextBlock textBlock(const QString &value, bool wrap = true)
{
    TextBlock text;
    text.Text(hstring(value.toStdWString()));
    if (wrap) {
        text.TextWrapping(TextWrapping::Wrap);
    }
    return text;
}

StackPanel page(const QString &title, const QString &body)
{
    StackPanel content;
    content.Spacing(16);
    content.MaxWidth(660);
    content.HorizontalAlignment(HorizontalAlignment::Stretch);

    TextBlock heading = textBlock(title);
    heading.Style(Application::Current().Resources()
                      .Lookup(box_value(L"TitleTextBlockStyle"))
                      .as<Style>());
    content.Children().Append(heading);
    TextBlock description = textBlock(body);
    description.Opacity(0.72);
    content.Children().Append(description);
    return content;
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

StackPanel settingRow(const QString &label, const Control &control)
{
    StackPanel row;
    row.Spacing(6);
    TextBlock caption = textBlock(label, false);
    caption.Style(Application::Current().Resources()
                      .Lookup(box_value(L"BodyStrongTextBlockStyle"))
                      .as<Style>());
    row.Children().Append(caption);
    row.Children().Append(control);
    return row;
}

void showProviderStats(const StackPanel &panel, const QList<ProviderDescriptor> &providers,
                       const QString &id)
{
    panel.Children().Clear();
    for (const ProviderDescriptor &provider : providers) {
        if (provider.id != id) {
            continue;
        }
        for (const ProviderStat &stat : provider.stats) {
            TextBlock row = textBlock(stat.label + QStringLiteral(": ") + stat.value);
            row.FontSize(12);
            row.Opacity(0.72);
            panel.Children().Append(row);
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
            QStringLiteral("This assistant checks your transcription provider, microphone, desktop accessibility, text delivery, refinement, and writing profiles.")};
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
            if (microphoneStatus && value > 0.01f) {
                microphoneStatus.Text(L"Microphone input detected.");
            }
        });
        QObject::connect(microphone, &AudioInput::failed, q, [this](const QString &message) {
            if (microphoneStatus) {
                microphoneStatus.Text(hstring(message.toStdWString()));
            }
            if (microphoneProblem) {
                microphoneProblem.IsOpen(true);
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
        ++transcriptionCheckGeneration;
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
        skip.Visibility(singlePage || index == 8 ? Visibility::Collapsed : Visibility::Visible);
        back.Visibility(singlePage || index == 0 ? Visibility::Collapsed : Visibility::Visible);
        next.Content(box_value(singlePage ? L"Done" : (index == 8 ? L"Get started" : L"Next")));
    }

    void showWelcome()
    {
        const QStringList copy = welcomeCopy();
        StackPanel panel = page(
            QStringLiteral("Welcome to Speecher"),
            copy.at(0));
        panel.Children().Append(textBlock(copy.at(1)));
        content.Children().Append(panel);
    }

    void showTranscription()
    {
        StackPanel panel = page(
            QStringLiteral("Transcription"),
            QStringLiteral("Choose the service Speecher uses to turn speech into a Raw Transcript."));
        QList<QPair<QString, QString>> options;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->speechProviders()) {
            options.append({provider.id, provider.label});
        }
        ComboBox provider = combo(options, controller->settings()->speechProvider());
        CheckBox accuracy;
        accuracy.Content(box_value(L"Accuracy pass: retranscribe the whole recording when you stop"));
        accuracy.IsChecked(controller->settings()->codexFinalRetranscribe());
        accuracy.Click([this, accuracy](const auto &, const auto &) {
            controller->settings()->setCodexFinalRetranscribe(accuracy.IsChecked().Value());
        });
        TextBlock accuracyHelp = textBlock(
            QStringLiteral("The live preview is unchanged; a second, whole-recording transcription "
                           "fixes words the live pass misheard. Adds about one to five seconds after "
                           "you stop. Dictations longer than about a minute and a half keep the live "
                           "transcript."),
            true);
        StackPanel stats;
        stats.Spacing(4);
        TextBlock status = textBlock(QString(), true);
        Button check;
        check.Content(box_value(L"Check again"));
        const auto runCheck = [this, provider, status, options, stats, accuracy, accuracyHelp] {
            const quint64 generation = ++transcriptionCheckGeneration;
            const int index = provider.SelectedIndex();
            if (index < 0 || index >= options.size()) {
                status.Text(L"No transcription service is available.");
                return;
            }
            const QString id = options.at(index).first;
            showProviderStats(stats, controller->providerRegistry()->speechProviders(), id);
            controller->settings()->setSpeechProvider(id);
            const auto accuracyVisibility = id == QStringLiteral("codex")
                ? Visibility::Visible : Visibility::Collapsed;
            accuracy.Visibility(accuracyVisibility);
            accuracyHelp.Visibility(accuracyVisibility);
            SpeechTranscriber *transcriber = controller->providerRegistry()->speechProvider(id);
            if (!transcriber) {
                status.Text(L"No transcription service is available.");
                return;
            }
            status.Text(L"Checking...");
            const SpeechSettings settings = controller->settings()->snapshot().speech;
            if (std::optional<SpeechPrepareJob> job = transcriber->createPrepareJob(settings);
                job && job->run) {
                const QString label = transcriber->label();
                auto result = std::make_shared<SpeechPrepareResult>();
                auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
                QThread *thread = QThread::create([prepareJob, result] {
                    *result = prepareJob->run();
                });
                QObject::connect(thread, &QThread::finished, setup,
                                 [this, generation, id, label, provider, status,
                                  options, prepareJob, result] {
                    const int selected = provider.SelectedIndex();
                    if (generation != transcriptionCheckGeneration
                        || selected < 0 || selected >= options.size()
                        || options.at(selected).first != id) {
                        return;
                    }
                    if (prepareJob->apply) {
                        prepareJob->apply(*result);
                    }
                    status.Text(hstring((result->ok
                                             ? QStringLiteral("%1 is ready.").arg(label)
                                             : result->message)
                                            .toStdWString()));
                });
                QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
                thread->start();
                return;
            }
            const SpeechPrepareResult result = transcriber->prepare(settings);
            status.Text(hstring((result.ok
                                     ? QStringLiteral("%1 is ready.").arg(transcriber->label())
                                     : result.message)
                                    .toStdWString()));
        };
        provider.SelectionChanged([runCheck](const auto &, const auto &) { runCheck(); });
        check.Click([runCheck](const auto &, const auto &) { runCheck(); });
        panel.Children().Append(settingRow(QStringLiteral("Transcription service"), provider));
        panel.Children().Append(accuracy);
        panel.Children().Append(accuracyHelp);
        panel.Children().Append(stats);
        panel.Children().Append(status);
        panel.Children().Append(check);
        content.Children().Append(panel);
        runCheck();
    }

    void showMicrophone()
    {
        StackPanel panel = page(
            QStringLiteral("Microphone"),
            QStringLiteral("Choose the input Speecher should record. Speak normally and check that the level moves."));
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
        microphoneStatus = textBlock(QStringLiteral("Listening for microphone input..."));
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
        panel.Children().Append(settingRow(QStringLiteral("Microphone"), device));
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
        CheckBox restore;
        restore.Content(box_value(L"Restore the previous clipboard after typing"));
        restore.IsChecked(controller->settings()->restoreClipboardAfterTyping());
        restore.Click([this, restore](const auto &, const auto &) {
            controller->settings()->setRestoreClipboardAfterTyping(restore.IsChecked().Value());
        });
        panel.Children().Append(settingRow(QStringLiteral("Clipboard format"), format));
        panel.Children().Append(restore);
        content.Children().Append(panel);
    }

    void showRefinement()
    {
        StackPanel panel = page(
            QStringLiteral("Refinement"),
            QStringLiteral("Refinement can clean up a Raw Transcript after dictation. Choose a provider, or None to skip cleanup."));
        QList<QPair<QString, QString>> options;
        for (const ProviderDescriptor &provider : controller->providerRegistry()->refinementProviders()) {
            options.append({provider.id, provider.label});
        }
        options.append({QStringLiteral("none"), QStringLiteral("None")});
        ComboBox provider = combo(options, controller->settings()->refinementProvider());
        StackPanel stats;
        stats.Spacing(4);
        CheckBox fast;
        fast.Content(box_value(L"Fast mode"));
        const auto refreshFast = [this, provider, fast, options, stats] {
            const QString id = options.at(provider.SelectedIndex()).first;
            showProviderStats(stats, controller->providerRegistry()->refinementProviders(), id);
            fast.Visibility(id == QStringLiteral("openai") || id == QStringLiteral("anthropic")
                                ? Visibility::Visible : Visibility::Collapsed);
            if (id == QStringLiteral("openai")) {
                fast.IsChecked(controller->settings()->openAiFastMode());
            } else if (id == QStringLiteral("anthropic")) {
                fast.IsChecked(controller->settings()->anthropicFastMode());
            }
        };
        provider.SelectionChanged([this, provider, options, refreshFast](const auto &, const auto &) {
            controller->settings()->setRefinementProvider(options.at(provider.SelectedIndex()).first);
            refreshFast();
        });
        fast.Click([this, provider, fast, options](const auto &, const auto &) {
            const bool checked = fast.IsChecked().Value();
            const QString id = options.at(provider.SelectedIndex()).first;
            if (id == QStringLiteral("openai")) {
                controller->settings()->setOpenAiFastMode(checked);
            } else if (id == QStringLiteral("anthropic")) {
                controller->settings()->setAnthropicFastMode(checked);
            }
        });
        panel.Children().Append(settingRow(QStringLiteral("Provider"), provider));
        panel.Children().Append(stats);
        panel.Children().Append(fast);
        content.Children().Append(panel);
        refreshFast();
    }

    void showProfiles()
    {
        StackPanel panel = page(
            QStringLiteral("Writing profiles"),
            QStringLiteral("Choose the fallback Writing Profile and how much cleanup and tone adjustment each profile receives."));
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
        content.Children().Append(panel);
    }

    void showShortcut()
    {
        StackPanel panel = page(
            QStringLiteral("Global Shortcut"),
            QStringLiteral("Tap the shortcut to start dictation and tap it again to stop, or hold it and talk. Dictation ends when you let go."));
        TextBox recorder;
        recorder.IsReadOnly(true);
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
        panel.Children().Append(settingRow(QStringLiteral("Dictation shortcut"), recorder));

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

        panel.Children().Append(shortcutStatus);
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

    void showReady()
    {
        // The actual binding, not a hardcoded default: the shortcut step may
        // have recorded anything, a single key included.
        const QString display = controller->globalShortcutDisplay();
        StackPanel panel = page(
            QStringLiteral("Ready to dictate"),
            display.isEmpty()
                ? QStringLiteral("Set a dictation shortcut to start dictating from anywhere.")
                : QStringLiteral("Press %1 to start dictating; hold it to talk.").arg(display));
        panel.Children().Append(textBlock(QStringLiteral(
            "Speecher stays in the notification area. Open its microphone icon for status, your latest transcript, and settings.")));
        content.Children().Append(panel);
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
        if (!skipped) {
            controller->settings()->setLaunchAtLogin(launchAtLogin);
            if (controller->globalShortcut().isEmpty()) {
                QString error;
                const QKeySequence shortcut = WinGlobalShortcutBinder::defaultShortcut();
                if (!controller->setGlobalShortcut(shortcut, &error)) {
                    showPage(shortcutPage);
                    shortcutStatus.Text(hstring(QStringLiteral("Could not register the shortcut: %1")
                                                    .arg(error).toStdWString()));
                    return;
                }
            }
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
    quint64 transcriptionCheckGeneration = 0;
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
