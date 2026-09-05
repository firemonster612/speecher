#include "frontend/win/CustomRows.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "frontend/win/SettingsModel.h"
#include "frontend/win/SettingsPage.h"
#include "providers/ClaudeCredentials.h"
#include "providers/CliProxyCredentials.h"

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
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
const QString kCleanupColumn = QStringLiteral("cleanup");
const QString kToneColumn = QStringLiteral("tone");
// W1's OutputMethod id for SendInput Ctrl+V. Named here until the core knows
// it, so the picker already offers the Windows paste path.
const QString kWinPaste = QStringLiteral("win-paste");

QList<RowOption> outputMethods()
{
    // No ydotool entry: the virtual keyboard is Linux's, and a method Windows
    // cannot offer has no business being offered here.
    QList<RowOption> methods;
    for (const QString &method : {QString::fromLatin1(OutputMethod::Automatic),
                                  QString::fromLatin1(OutputMethod::DirectInsert),
                                  kWinPaste,
                                  QString::fromLatin1(OutputMethod::QtClipboard)}) {
        methods.append({method,
                        method == kWinPaste ? QStringLiteral("Paste with the keyboard")
                                            : OutputMethod::label(method)});
    }
    return methods;
}

QList<RowOption> cliproxyAccounts(const QString &type,
                                  const QString &selected,
                                  const SettingsStore &store)
{
    const QString directory = store.cliproxyOauthDir();
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    QList<RowOption> options;
    // With several accounts and none chosen yet, force an explicit choice
    // instead of silently pinning whichever file sorts first.
    if (selected.isEmpty() && accounts.size() > 1) {
        options.append({QString(), QStringLiteral("Choose an account…")});
    }
    bool selectedFound = selected.isEmpty();
    for (const CliProxyAccount &account : accounts) {
        options.append({account.fileName,
                        account.expired ? account.label + QStringLiteral(" (expired)") : account.label,
                        account.disabled ? QStringLiteral("Disabled in CLI Proxy API") : QString(),
                        !account.disabled});
        selectedFound = selectedFound || account.fileName == selected;
    }
    // Keep a stored selection visible even if its file is currently missing.
    if (!selectedFound) {
        options.append({selected, selected + QStringLiteral(" (missing)")});
    }
    if (options.isEmpty()) {
        options.append({QString(), QStringLiteral("No accounts found"), directory, false});
    }
    return options;
}

QList<RowOption> cleanupStrengths()
{
    return {
        {QStringLiteral("none"), QStringLiteral("None")},
        {QStringLiteral("light_cleanup"), QStringLiteral("Light")},
        {QStringLiteral("balanced"), QStringLiteral("Medium")},
        {QStringLiteral("strong_polish"), QStringLiteral("High")},
    };
}

QList<RowOption> writingTones()
{
    return {
        {QStringLiteral("none"), QStringLiteral("No tone override")},
        {QStringLiteral("formal"), QStringLiteral("Formal")},
        {QStringLiteral("casual"), QStringLiteral("Casual")},
        {QStringLiteral("very_casual"), QStringLiteral("Very casual")},
        {QStringLiteral("excited"), QStringLiteral("Excited")},
        {QStringLiteral("gen_z"), QStringLiteral("Gen Z")},
    };
}

TextBlock secondaryText(const QString &text)
{
    TextBlock block;
    block.Style(Application::Current()
                    .Resources()
                    .Lookup(box_value(L"SettingsCardDescriptionStyle"))
                    .as<Style>());
    block.Text(hs(text));
    return block;
}

// Free text with a commit on Enter or blur, shared by the two CLI Proxy rows.
TextBox commitTextBox(const RowSnapshot &row, PaneHost &host, const wchar_t *placeholder)
{
    TextBox box;
    box.MinWidth(240);
    box.PlaceholderText(placeholder);
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

PasswordBox commitPasswordBox(const RowSnapshot &row, PaneHost &host, const wchar_t *placeholder)
{
    PasswordBox box;
    box.MinWidth(240);
    box.PlaceholderText(placeholder);
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
        return secondaryText(host.model->credentialStatus());
    }
    StackPanel panel;
    panel.Spacing(4);
    PasswordBox box;
    box.MinWidth(240);
    box.PlaceholderText(L"Enter OpenAI API key");
    box.Password(hs(host.apiKey));
    box.PasswordChanged([&host](const IInspectable &sender, const auto &) {
        // A keyring read that lands after typing started must not clobber it;
        // the edit counter is what the deferred read checks.
        host.apiKey = qs(sender.as<PasswordBox>().Password());
        ++host.apiKeyEdits;
    });
    const auto save = [&host] {
        if (!host.apiKeyLoaded && host.apiKeyEdits == 0) {
            return;
        }
        host.credentialProblem = host.model->saveApiKey(host.apiKey);
        if (!host.credentialProblem.isEmpty()) {
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
        panel.Children().Append(secondaryText(host.credentialProblem));
    }
    return panel;
}

// One row per writing profile, each with its cleanup and tone pickers — the
// mac WritingProfileRows over the same grid descriptor.
UIElement writingProfileRows(const RowSnapshot &row, PaneHost &host)
{
    StackPanel rows;
    const QList<QVariantMap> records = row.value.value<QList<QVariantMap>>();
    QList<CollectionColumnSnapshot> choices;
    if (row.collection) {
        for (const CollectionColumnSnapshot &column : row.collection->columns) {
            if (column.kind == ColumnKind::Choice) {
                choices.append(column);
            }
        }
    }
    for (qsizetype index = 0; index < records.size(); ++index) {
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
            combo.SelectionChanged([rowId = row.id, records, index, columnId = column.id, &host](
                                       const IInspectable &sender, const auto &) {
                const auto item = sender.as<ComboBox>().SelectedItem();
                if (!item) {
                    return;
                }
                QList<QVariantMap> edited = records;
                edited[index].insert(columnId,
                                     qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag())));
                host.model->save(edited, rowId);
                host.refresh();
            });
            pickers.Children().Append(combo);
        }
        RowSnapshot profileRow;
        profileRow.id = row.id + QLatin1Char('.') + records.at(index).value(kProfileIdKey).toString();
        profileRow.label = records.at(index).value(kProfileColumn).toString();
        rows.Children().Append(rowGrid(profileRow, pickers, host, index > 0));
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
        TextBlock text;
        text.Style(Application::Current()
                       .Resources()
                       .Lookup(box_value(L"SettingsCardBodyStyle"))
                       .as<Style>());
        text.Text(hs(lines.join(QLatin1Char('\n'))));
        text.IsTextSelectionEnabled(true);
        if (heading) {
            text.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        }
        notes.Children().Append(text);
    }
    return notes;
}

} // namespace

QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store)
{
    if (rowId == QStringLiteral("outputMethod")) {
        return outputMethods();
    }
    if (rowId == QStringLiteral("openAiAuthMode")) {
        return {
            {QStringLiteral("auto"), QStringLiteral("Automatic")},
            {QStringLiteral("codex_api_key"), QStringLiteral("API key from the Codex app")},
            {QStringLiteral("codex_oauth"), QStringLiteral("ChatGPT sign-in from the Codex app")},
            {QStringLiteral("env"), QStringLiteral("API key from the environment")},
            {QStringLiteral("settings"), QStringLiteral("API key saved in Speecher")},
            {kCliProxyAuthMode, QStringLiteral("CLI Proxy API account")},
        };
    }
    if (rowId == QStringLiteral("anthropicAuthMode")) {
        return {
            {QStringLiteral("oauth"), QStringLiteral("Claude Code sign-in")},
            {kCliProxyAuthMode, QStringLiteral("CLI Proxy API account")},
        };
    }
    if (rowId == QStringLiteral("openAiCliproxyAccount")) {
        return cliproxyAccounts(QStringLiteral("codex"), draft.refinement.openAiCliproxyAccount, store);
    }
    if (rowId == QStringLiteral("anthropicCliproxyAccount")) {
        return cliproxyAccounts(QStringLiteral("claude"), draft.refinement.anthropicCliproxyAccount, store);
    }
    return {};
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

CollectionDescriptor writingProfileGrid()
{
    CollectionDescriptor grid;
    grid.columns = {
        {kProfileColumn, QStringLiteral("Profile"), ColumnKind::ReadOnly},
        {kCleanupColumn, QStringLiteral("Cleanup"), ColumnKind::Choice, cleanupStrengths},
        {kToneColumn, QStringLiteral("Tone"), ColumnKind::Choice, writingTones, true},
    };
    // The profiles are the ones that exist, so the stored list only says what
    // each of them was set to.
    grid.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
            const WritingProfileSettings chosen =
                writingProfileSettingsFor(settings.refinement.writingProfiles, fallback.profile);
            records.append({{kProfileColumn, writingProfileLabel(fallback.profile)},
                            {kProfileIdKey, writingProfileName(fallback.profile)},
                            {kCleanupColumn, chosen.cleanupStrength},
                            {kToneColumn, chosen.tone}});
        }
        return records;
    };
    grid.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        QList<WritingProfileSettings> profiles;
        for (const QVariantMap &record : records) {
            profiles.append({writingProfileFromName(record.value(kProfileIdKey).toString()),
                             record.value(kCleanupColumn).toString(),
                             record.value(kToneColumn).toString()});
        }
        settings.refinement.writingProfiles = profiles;
    };
    return grid;
}

bool customRowIsFullWidth(const QString &rowId)
{
    return rowId == QStringLiteral("writingProfileBehavior")
        || rowId == QStringLiteral("whatsNewNotes");
}

UIElement customRowElement(const RowSnapshot &row, PaneHost &host)
{
    if (row.id == QStringLiteral("writingProfileBehavior")) {
        return writingProfileRows(row, host);
    }
    if (row.id == QStringLiteral("whatsNewNotes")) {
        return releaseNotes(row);
    }
    if (row.id == QStringLiteral("openAiAuth")) {
        return credentialField(host);
    }
    if (row.id == QStringLiteral("anthropicAuthMode")) {
        StackPanel panel;
        panel.Spacing(4);
        ComboBox combo = choiceComboBox(row, host);
        combo.HorizontalAlignment(HorizontalAlignment::Right);
        panel.Children().Append(combo);
        const QString status = host.model->anthropicCredentialStatus();
        if (!status.isEmpty()) {
            TextBlock text = secondaryText(status);
            text.HorizontalAlignment(HorizontalAlignment::Right);
            text.TextAlignment(TextAlignment::End);
            panel.Children().Append(text);
        }
        return panel;
    }
    if (row.id == QStringLiteral("cliproxyBaseUrl")) {
        return commitTextBox(row, host, L"Leave empty to use the account files on this computer");
    }
    if (row.id == QStringLiteral("cliproxyApiKey")) {
        return commitPasswordBox(row, host, L"A key the server accepts");
    }
    // The fallback the mac renderer uses: a picker when the row supplied
    // choices, a text field when it holds text, nothing otherwise.
    if (!row.options.isEmpty()) {
        return choiceComboBox(row, host);
    }
    if (row.value.typeId() == QMetaType::QString) {
        return commitTextBox(row, host, L"");
    }
    return nullptr;
}

} // namespace speecher::win
