#include "frontend/win/CustomRows.h"
#include "frontend/win/ShortcutRecorder.h"

#include "app/ApplicationController.h"
#include "app/MicrophoneTest.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "dictation/DictationTypes.h"
#include "frontend/win/LocalModelBrowser.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/SettingsPage.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ProviderRegistry.h"
#include "providers/ProviderSignIn.h"

#include <QRegularExpression>

#include <algorithm>
#include <memory>
#include <optional>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

const QString kCliProxyAuthMode = QStringLiteral("cliproxy");
const QString kProfileColumn = QStringLiteral("profile");
const QString kProfileIdKey = QStringLiteral("profileId");

TextBlock secondaryText(const QString &text, const PaneHost &host)
{
    return secondaryTextBlock(text, L"SettingsCardDescriptionStyle", host);
}

// A profile's fields are read one at a time, so each names its profile and
// its column.
void nameProfileField(const UIElement &field, const QString &profile, const QString &column)
{
    QStringList name{profile, column};
    name.removeAll(QString());
    Automation::AutomationProperties::SetName(field, hs(name.join(QStringLiteral(", "))));
}

// Free text with a commit on Enter or blur, for the CLI Proxy rows.
TextBox commitTextBox(const RowSnapshot &row, PaneHost &host)
{
    TextBox box;
    box.MinWidth(kWideControlWidth);
    box.PlaceholderText(hs(row.placeholder));
    box.Text(hs(row.value.toString()));
    const auto commit = [rowId = row.id, stored = row.value.toString(), &host](const TextBox &box) {
        const QString text = qs(box.Text());
        if (text != stored) {
            setValueAndCommit(host, rowId, text);
        }
    };
    box.LostFocus([commit](const IInspectable &sender, const auto &) {
        commit(sender.as<TextBox>());
    });
    box.KeyDown([commit](const IInspectable &sender, const Input::KeyRoutedEventArgs &args) {
        if (args.Key() == Windows::System::VirtualKey::Enter) {
            commit(sender.as<TextBox>());
        }
    });
    return box;
}

PasswordBox commitPasswordBox(const RowSnapshot &row, PaneHost &host)
{
    PasswordBox box;
    box.MinWidth(kWideControlWidth);
    box.PlaceholderText(hs(row.placeholder));
    box.Password(hs(row.value.toString()));
    const auto commit = [rowId = row.id, stored = row.value.toString(), &host](
                            const PasswordBox &box) {
        const QString text = qs(box.Password());
        if (text != stored) {
            setValueAndCommit(host, rowId, text);
        }
    };
    box.LostFocus([commit](const IInspectable &sender, const auto &) {
        commit(sender.as<PasswordBox>());
    });
    box.KeyDown([commit](const IInspectable &sender, const Input::KeyRoutedEventArgs &args) {
        if (args.Key() == Windows::System::VirtualKey::Enter) {
            commit(sender.as<PasswordBox>());
        }
    });
    return box;
}

// The OpenAI credential: a secret to type while the app settings key is the
// chosen source, and the resolved status of whichever source it is otherwise.
UIElement credentialField(PaneHost &host)
{
    if (!host.model->credentialIsEditable()) {
        return secondaryText(host.model->credentialStatus(), host);
    }
    StackPanel panel;
    panel.Spacing(4);
    PasswordBox box;
    box.MinWidth(kWideControlWidth);
    box.PlaceholderText(L"Enter OpenAI API key");
    box.Password(hs(host.apiKey));
    box.PasswordChanged([&host](const IInspectable &sender, const auto &) {
        // A keyring read that lands after typing started must not clobber it;
        // the edit counter is what the deferred read checks.
        host.apiKey = qs(sender.as<PasswordBox>().Password());
        ++host.apiKeyEdits;
    });
    const auto save = [&host] {
        // Nothing typed means nothing to write: saving the keyring's own value
        // back would rewrite the credential on every focus loss.
        if (host.apiKeyEdits == 0) {
            return;
        }
        const QString problem = host.model->saveApiKey(host.apiKey);
        const bool changed = problem != host.credentialProblem;
        host.credentialProblem = problem;
        // Success after a shown error must also refresh, to clear the line.
        if (changed) {
            host.refresh();
        }
    };
    box.LostFocus([save](const auto &, const auto &) { save(); });
    box.KeyDown([save](const auto &, const Input::KeyRoutedEventArgs &args) {
        if (args.Key() == Windows::System::VirtualKey::Enter) {
            save();
        }
    });
    panel.Children().Append(box);
    if (!host.credentialProblem.isEmpty()) {
        TextBlock problem = secondaryText(host.credentialProblem, host);
        problem.MaxWidth(kWideControlWidth);
        panel.Children().Append(problem);
    }
    return panel;
}

// Deletes the custom profile at `index`, first saying what that changes when
// a rule or the fallback points at it.
void deleteWritingProfile(const QString &rowId, const QList<QVariantMap> &records, qsizetype index,
                          const QString &deleteLabel, PaneHost &host)
{
    const auto remove = [rowId, records, index, &host] {
        QList<QVariantMap> edited = records;
        edited.removeAt(index);
        host.model->save(edited, rowId, records);
        host.refresh();
    };
    const QString notice =
        host.model->writingProfileDeletionNotice(records.at(index).value(kProfileIdKey).toString());
    if (notice.isEmpty()) {
        remove();
        return;
    }
    ContentDialog dialog;
    dialog.XamlRoot(host.xamlRoot());
    dialog.Title(box_value(hs(writingProfileDeletionTitle())));
    dialog.Content(box_value(hs(notice)));
    dialog.PrimaryButtonText(hs(deleteLabel));
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Close);
    dialog.Closed([remove, weak = std::weak_ptr<bool>(host.alive)](const ContentDialog &,
                                                                    const ContentDialogClosedEventArgs &args) {
        if (args.Result() == ContentDialogResult::Primary && !gone(weak)) {
            remove();
        }
    });
    dialog.ShowAsync();
}

// One row per writing profile, each with its cleanup and tone pickers and its
// instructions under them — the mac WritingProfileRows over the same grid
// descriptor. A custom profile adds its name and a Delete button, and Add
// profile follows the rows.
UIElement writingProfileRows(const RowSnapshot &row, PaneHost &host)
{
    StackPanel rows;
    const QList<QVariantMap> records = row.value.value<QList<QVariantMap>>();
    QList<CollectionColumnSnapshot> choices;
    QList<CollectionColumnSnapshot> texts;
    QString profileTitle;
    if (row.collection) {
        for (const CollectionColumnSnapshot &column : row.collection->columns) {
            if (column.kind == ColumnKind::Choice) {
                choices.append(column);
            } else if (column.kind == ColumnKind::Text) {
                texts.append(column);
            } else if (column.id == kProfileColumn) {
                profileTitle = column.title;
            }
        }
    }
    for (qsizetype index = 0; index < records.size(); ++index) {
        const QString profile = records.at(index).value(kProfileColumn).toString();
        StackPanel pickers;
        pickers.Orientation(Orientation::Horizontal);
        pickers.Spacing(8);
        for (const CollectionColumnSnapshot &column : choices) {
            ComboBox combo;
            combo.MinWidth(140);
            int selected = -1;
            for (const RowOption &option : column.options) {
                ComboBoxItem item;
                item.Content(box_value(hs(option.label)));
                item.Tag(box_value(hs(option.id)));
                if (option.id == records.at(index).value(column.id).toString()) {
                    selected = combo.Items().Size();
                }
                combo.Items().Append(item);
            }
            combo.SelectedIndex(selected);
            nameProfileField(combo, profile, column.title);
            combo.SelectionChanged([rowId = row.id, records, index, columnId = column.id, &host](
                                       const IInspectable &sender, const auto &) {
                const auto item = sender.as<ComboBox>().SelectedItem();
                if (!item) {
                    return;
                }
                QList<QVariantMap> edited = records;
                edited[index].insert(columnId,
                                     qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag())));
                host.model->save(edited, rowId, records);
                host.refresh();
            });
            if (index == 0) {
                // The grid's column titles, once, directly above the first
                // row's pickers so each header sits over its own column.
                StackPanel titled;
                titled.Spacing(2);
                titled.Children().Append(secondaryText(column.title, host));
                titled.Children().Append(combo);
                pickers.Children().Append(titled);
            } else {
                pickers.Children().Append(combo);
            }
        }
        const bool custom = !isBuiltInWritingProfile(records.at(index).value(kProfileIdKey).toString());
        if (custom) {
            Button remove;
            remove.Content(box_value(hs(row.collection->deleteLabel)));
            remove.VerticalAlignment(VerticalAlignment::Bottom);
            remove.Click([rowId = row.id, records, index, deleteLabel = row.collection->deleteLabel,
                          &host](const auto &, const auto &) {
                deleteWritingProfile(rowId, records, index, deleteLabel, host);
            });
            pickers.Children().Append(remove);
        }
        StackPanel controls;
        controls.Spacing(8);
        if (custom) {
            TextBox name;
            name.PlaceholderText(L"Name");
            name.Text(hs(profile));
            nameProfileField(name, profile, profileTitle);
            name.LostFocus([rowId = row.id, records, index, &host](const IInspectable &sender, const auto &) {
                const QString text = qs(sender.as<TextBox>().Text());
                if (text == records.at(index).value(kProfileColumn).toString()) {
                    return;
                }
                QList<QVariantMap> edited = records;
                edited[index].insert(kProfileColumn, text);
                host.model->save(edited, rowId, records);
                host.refresh();
            });
            controls.Children().Append(name);
        }
        controls.Children().Append(pickers);
        for (const CollectionColumnSnapshot &column : texts) {
            TextBox box;
            box.PlaceholderText(hs(column.title));
            box.Text(hs(records.at(index).value(column.id).toString()));
            nameProfileField(box, profile, column.title);
            if (column.multiline) {
                makeMultiline(box);
            }
            box.LostFocus([rowId = row.id, records, index, columnId = column.id, &host](
                              const IInspectable &sender, const auto &) {
                const QString text = qs(sender.as<TextBox>().Text());
                if (text == records.at(index).value(columnId).toString()) {
                    return;
                }
                QList<QVariantMap> edited = records;
                edited[index].insert(columnId, text);
                host.model->save(edited, rowId, records);
                host.refresh();
            });
            controls.Children().Append(box);
        }
        RowSnapshot profileRow;
        profileRow.id = row.id + QLatin1Char('.') + records.at(index).value(kProfileIdKey).toString();
        profileRow.label = profile;
        rows.Children().Append(rowGrid(profileRow, controls, host, index > 0));
    }
    if (row.collection && !row.collection->addLabel.isEmpty()) {
        Button add;
        add.Content(box_value(hs(row.collection->addLabel)));
        add.HorizontalAlignment(HorizontalAlignment::Right);
        add.Margin({16, 8, 16, 12});
        add.Click([rowId = row.id, records, blank = row.collection->blankRecord, &host](const auto &,
                                                                                      const auto &) {
            host.model->save(records + QList<QVariantMap>{blank}, rowId, records);
            host.refresh();
        });
        rows.Children().Append(add);
    }
    return rows;
}

// The release notes: Markdown-ish paragraphs, --- separators, # lines bold,
// leading dashes as bullets.
UIElement releaseNotes(const RowSnapshot &row)
{
    StackPanel notes;
    notes.Padding({16, 16, 16, 16});
    notes.Spacing(8);
    const QStringList blocks = row.value.toString().split(QStringLiteral("\n\n"));
    for (const QString &block : blocks) {
        if (block.trimmed() == QStringLiteral("---")) {
            static const hstring divider = hstring(
                LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" )")
                + LR"(Height="1" Background="{ThemeResource DividerStrokeColorDefaultBrush}"/>)";
            notes.Children().Append(
                winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(divider).as<UIElement>());
            continue;
        }
        const bool heading = block.startsWith(QLatin1Char('#'));
        QStringList lines;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1Char('#'))) {
                qsizetype start = 0;
                while (start < line.size()
                       && (line.at(start) == QLatin1Char('#') || line.at(start) == QLatin1Char(' '))) {
                    ++start;
                }
                lines.append(line.mid(start));
            } else if (line.startsWith(QStringLiteral("- "))) {
                lines.append(QStringLiteral("• ") + line.mid(2));
            } else {
                lines.append(line);
            }
        }
        // Inline [text](url) links become Hyperlinks; the rest is plain runs.
        static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\(([^)\\s]+)\\)"));
        const QString source = lines.join(QLatin1Char('\n'));
        Documents::Paragraph paragraph;
        const auto appendRun = [&paragraph](const QString &text) {
            Documents::Run run;
            run.Text(hs(text));
            paragraph.Inlines().Append(run);
        };
        qsizetype consumed = 0;
        for (const QRegularExpressionMatch &match : link.globalMatch(source)) {
            appendRun(source.mid(consumed, match.capturedStart() - consumed));
            consumed = match.capturedEnd();
            // Uri throws on a relative or malformed target; such a link reads
            // as its text.
            std::optional<Uri> target;
            try {
                target.emplace(hs(match.captured(2)));
            } catch (const winrt::hresult_error &) {
            }
            if (!target) {
                appendRun(match.captured(1));
                continue;
            }
            Documents::Run label;
            label.Text(hs(match.captured(1)));
            Documents::Hyperlink hyperlink;
            hyperlink.NavigateUri(*target);
            hyperlink.Inlines().Append(label);
            paragraph.Inlines().Append(hyperlink);
        }
        appendRun(source.mid(consumed));
        // Body text by default, as SettingsCardBodyStyle's TextBlocks are.
        RichTextBlock text;
        text.TextWrapping(TextWrapping::Wrap);
        text.IsTextSelectionEnabled(true);
        if (heading) {
            text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        }
        text.Blocks().Append(paragraph);
        notes.Children().Append(text);
    }
    return notes;
}

} // namespace

QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store)
{
    if (rowId == QStringLiteral("openAiCliproxyAccount")) {
        return cliproxyAccountOptions(ProviderSignIn::cliproxyAccountType(QStringLiteral("openai")),
                                      draft.refinement.openAiCliproxyAccount,
                                      store.cliproxyOauthDir());
    }
    if (rowId == QStringLiteral("anthropicCliproxyAccount")) {
        return cliproxyAccountOptions(ProviderSignIn::cliproxyAccountType(QStringLiteral("anthropic")),
                                      draft.refinement.anthropicCliproxyAccount,
                                      store.cliproxyOauthDir());
    }
    return authModeOptions(rowId);
}

QString anthropicCredentialStatus(const AppSettings &draft, const SettingsStore &store)
{
    if (draft.refinement.anthropicAuthMode == kCliProxyAuthMode) {
        return {};
    }
    const ClaudeCredentialResult credentials =
        ClaudeCredentials::load(store.claudeCredentialsPath(), false);
    return credentials.ok ? QStringLiteral("Signed in with Claude Code") : credentials.error;
}

namespace {

// The Test microphone row: the input device's live level beside the button
// that starts and stops the test, and under them why the device would not
// open. The test lives on the host, so a rebuild of the pane redraws it
// rather than ending it; the window ends it on a pane change and on close.
UIElement microphoneTestElement(const RowSnapshot &row, PaneHost &host)
{
    if (!host.microphoneTest) {
        host.microphoneTest = std::make_shared<MicrophoneTest>(*host.controller);
    }
    MicrophoneTest *test = host.microphoneTest.get();
    // Only the newest drawing of the row listens.
    test->disconnect();

    ProgressBar level;
    level.Minimum(0);
    level.Maximum(1);
    level.Width(160);
    level.VerticalAlignment(VerticalAlignment::Center);
    Automation::AutomationProperties::SetName(level, hs(inputLevelLabel()));
    Button button;
    StackPanel controls;
    controls.Orientation(Orientation::Horizontal);
    controls.Spacing(8);
    controls.Children().Append(level);
    controls.Children().Append(button);
    TextBlock problem = secondaryText(QString(), host);
    problem.TextWrapping(TextWrapping::Wrap);
    problem.Visibility(Visibility::Collapsed);
    StackPanel element;
    element.Spacing(4);
    element.Children().Append(controls);
    element.Children().Append(problem);

    // Not a Control, so the row cannot close its gate on this element; the
    // button takes the gate along with the test's own state.
    const auto follow = [test, level, button, problem, gateOpen = row.enabled] {
        button.Content(box_value(hs(microphoneTestCaption(test->state()))));
        button.IsEnabled(gateOpen && test->canToggle());
        if (test->state() != MicrophoneTestState::Running) {
            level.Value(0);
        }
        if (test->state() == MicrophoneTestState::Starting) {
            problem.Visibility(Visibility::Collapsed);
        }
    };
    follow();
    QObject::connect(test, &MicrophoneTest::changed, test, follow);
    QObject::connect(test, &MicrophoneTest::levelChanged, test, [level](float value) {
        level.Value(std::clamp(value, 0.0f, 1.0f));
    });
    QObject::connect(test, &MicrophoneTest::failed, test, [problem](const QString &message) {
        problem.Text(hs(message));
        problem.Visibility(Visibility::Visible);
    });
    // The device row saves as it is chosen, so the saved device is the one shown.
    button.Click([weak = std::weak_ptr<MicrophoneTest>(host.microphoneTest),
                  controller = host.controller](const auto &, const auto &) {
        if (const auto test = weak.lock()) {
            test->toggle(controller->settings()->audioInputDeviceId());
        }
    });
    return element;
}

// One of a fallback row's tool buttons: the platform's glyph, named with
// core's caption, which is also its tooltip.
Button fallbackTool(wchar_t glyph, const QString &caption, const QString &item, bool enabled)
{
    Button button;
    FontIcon icon;
    icon.Glyph(hstring(std::wstring_view(&glyph, 1)));
    icon.FontSize(16);
    button.Content(icon);
    button.IsEnabled(enabled);
    Automation::AutomationProperties::SetName(button, hs(caption));
    Automation::AutomationProperties::SetHelpText(button, hs(item));
    ToolTipService::SetToolTip(button, box_value(hs(caption)));
    return button;
}

} // namespace

QList<RowOption> chainProviders(ProviderRole role, const ProviderRegistry &registry)
{
    QList<RowOption> providers;
    const QList<ProviderDescriptor> descriptors =
        role == ProviderRole::Speech ? registry.speechProviders() : registry.refinementProviders();
    for (const ProviderDescriptor &provider : descriptors) {
        providers.append({provider.id, provider.label});
    }
    return providers;
}

UIElement fallbackListElement(ProviderRole role,
                               const FallbackListPresentation &list,
                               const AppSettings &settings,
                               const std::function<void(const QStringList &)> &write,
                               PaneHost &host)
{
    StackPanel element;
    if (list.heading.isEmpty()) {
        return element;
    }
    element.Children().Append(styledTextBlock(list.heading, L"SettingsSectionHeaderStyle"));
    if (!list.subtitle.isEmpty()) {
        TextBlock subtitle = secondaryText(list.subtitle, host);
        subtitle.Margin({1, 0, 0, 8});
        element.Children().Append(subtitle);
    }
    // Shared by every button's edit, which works from the settings shown.
    const auto shown = std::make_shared<const AppSettings>(settings);
    StackPanel rows;
    for (qsizetype index = 0; index < list.items.size(); ++index) {
        const FallbackItem &item = list.items.at(index);
        StackPanel tools;
        tools.Orientation(Orientation::Horizontal);
        tools.Spacing(4);
        const int at = int(index);
        Button up = fallbackTool(L'\uE74A', list.moveUpCaption, item.label, item.canMoveUp);
        up.Click([=](const auto &, const auto &) { write(withFallbackMoved(*shown, role, at, -1)); });
        Button down = fallbackTool(L'\uE74B', list.moveDownCaption, item.label, item.canMoveDown);
        down.Click([=](const auto &, const auto &) { write(withFallbackMoved(*shown, role, at, 1)); });
        Button remove = fallbackTool(L'\uE74D', list.removeCaption, item.label, true);
        remove.Click([=](const auto &, const auto &) { write(withFallbackRemoved(*shown, role, at)); });
        tools.Children().Append(up);
        tools.Children().Append(down);
        tools.Children().Append(remove);
        RowSnapshot row;
        row.label = item.label;
        row.help = item.status;
        row.helpTone = item.tone;
        rows.Children().Append(rowGrid(row, tools, host, index > 0));
    }
    if (list.canAdd) {
        ComboBox add;
        add.MinWidth(200);
        add.PlaceholderText(hs(list.addPlaceholder));
        for (const RowOption &choice : list.addChoices) {
            ComboBoxItem item;
            item.Content(box_value(hs(choice.label)));
            item.Tag(box_value(hs(choice.id)));
            add.Items().Append(item);
        }
        add.SelectionChanged([=](const IInspectable &sender, const auto &) {
            if (const auto item = sender.as<ComboBox>().SelectedItem()) {
                write(withFallbackAdded(*shown, role,
                                        qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag()))));
            }
        });
        RowSnapshot row;
        row.label = list.addLabel;
        row.help = list.addHelp;
        rows.Children().Append(rowGrid(row, add, host, !list.items.isEmpty()));
    }
    if (rows.Children().Size() > 0) {
        element.Children().Append(cardContainer(rows));
    }
    if (!list.footer.isEmpty()) {
        element.Children().Append(secondaryTextBlock(list.footer, L"SettingsFootnoteStyle", host));
    }
    return element;
}

void endMicrophoneTest(PaneHost &host)
{
    if (!host.microphoneTest) {
        return;
    }
    host.microphoneTest->stop();
    host.microphoneTest->disconnect();
    host.microphoneTest.reset();
}

bool customRowIsSection(const QString &rowId)
{
    return rowId == QStringLiteral("speechFallbackList") || rowId == QStringLiteral("refinementFallbackList");
}

bool customRowIsFullWidth(const QString &rowId)
{
    return rowId == QStringLiteral("writingProfileBehavior")
        || rowId == QStringLiteral("whatsNewNotes")
        || rowId == QStringLiteral("localModelBrowser")
        || rowId == QStringLiteral("globalShortcut")
        || rowId == QStringLiteral("cancelShortcut");
}

UIElement customRowElement(const RowSnapshot &row, PaneHost &host)
{
    if (row.id == QStringLiteral("writingProfileBehavior")) {
        return writingProfileRows(row, host);
    }
    if (row.id == QStringLiteral("whatsNewNotes")) {
        return releaseNotes(row);
    }
    if (customRowIsSection(row.id)) {
        const ProviderRole role = row.id == QStringLiteral("speechFallbackList") ? ProviderRole::Speech
                                                                                 : ProviderRole::Refinement;
        return fallbackListElement(role, host.model->fallbackList(role), host.model->draft(),
                                   [rowId = row.id, &host](const QStringList &fallbacks) {
                                       setValueAndCommit(host, rowId, fallbacks);
                                   },
                                   host);
    }
    if (row.id == QStringLiteral("globalShortcut") || row.id == QStringLiteral("cancelShortcut")) {
        return ShortcutRecorder::element(row, host);
    }
    if (row.id == QStringLiteral("localModelBrowser")) {
        if (!host.localModels) {
            host.localModels = std::make_shared<LocalModelBrowser>(host);
        }
        return host.localModels->element(row);
    }
    if (row.id == QStringLiteral("openAiAuth")) {
        return credentialField(host);
    }
    if (row.id == QStringLiteral("microphoneTest")) {
        return microphoneTestElement(row, host);
    }
    if (row.id == QStringLiteral("anthropicAuth")) {
        return secondaryText(host.model->anthropicCredentialStatus(), host);
    }
    // The fallback the mac renderer uses: a picker when the row supplied
    // choices, a text field when it holds text, nothing otherwise.
    if (!row.options.isEmpty()) {
        return choiceComboBox(row, host);
    }
    if (row.secret) {
        return commitPasswordBox(row, host);
    }
    if (row.value.typeId() == QMetaType::QString) {
        return commitTextBox(row, host);
    }
    return nullptr;
}

} // namespace speecher::win
