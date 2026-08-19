#include "core/settings/SettingsSchema.h"

namespace speecher {

namespace {

using Getter = std::function<QString(const AppSettings &)>;
using Setter = std::function<void(AppSettings &, const QString &)>;
using Options = std::function<QList<RowOption>(const AppSettings &)>;

const QString kMatchColumn = QStringLiteral("match");
const QString kCategoryColumn = QStringLiteral("category");
const QString kProfileColumn = QStringLiteral("profile");
const QString kSourceColumn = QStringLiteral("source");
const QString kEnabledColumn = QStringLiteral("enabled");
const QString kApplicationColumn = QStringLiteral("application");
const QString kMethodColumn = QStringLiteral("method");

// The paste-method value a category row carries when it defers to the global
// fallback, which is stored as the absence of a rule.
QString inheritGlobalPasteRule()
{
    return QStringLiteral("inherit");
}

// The paste chord differs per platform, and so does the copy that names it.
QList<RowOption> pasteMethodOptions(bool includeDirectInsert, bool includeGlobalFallback)
{
    QList<RowOption> options;
    if (includeGlobalFallback) {
        options.append({inheritGlobalPasteRule(), QStringLiteral("Use global fallback")});
    }
#ifdef Q_OS_MACOS
    // Terminals on macOS take the same Cmd+V as every other app, so the two
    // paste chords name the same keystroke here.
    options.append({pasteMethodName(PasteMethod::StandardPaste), QStringLiteral("Standard paste (Cmd+V)")});
    options.append({pasteMethodName(PasteMethod::TerminalPaste), QStringLiteral("Terminal paste (Cmd+V)")});
    if (includeDirectInsert) {
        options.append({pasteMethodName(PasteMethod::DirectInsert),
                        QStringLiteral("Direct insertion (Accessibility)")});
    }
#else
    options.append({pasteMethodName(PasteMethod::StandardPaste), QStringLiteral("Standard paste (Ctrl+V)")});
    options.append({pasteMethodName(PasteMethod::TerminalPaste),
                    QStringLiteral("Terminal paste (Ctrl+Shift+V)")});
    if (includeDirectInsert) {
        options.append({pasteMethodName(PasteMethod::DirectInsert),
                        QStringLiteral("Direct insertion (AT-SPI)")});
    }
#endif
    options.append({pasteMethodName(PasteMethod::ClipboardOnly), QStringLiteral("Clipboard only")});
    return options;
}

QString applicationIdHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Use the app's bundle identifier, such as com.apple.Terminal.");
#else
    return QStringLiteral("Use the desktop application ID reported by AT-SPI.");
#endif
}

QString applicationPasteRuleHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Override paste behavior for an exact bundle identifier, such as com.apple.Terminal.");
#else
    return QStringLiteral("Override paste behavior for an exact application ID, such as org.kde.konsole.");
#endif
}

QString targetAccessibilityHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Grant Accessibility permission so Speecher can identify the target application.");
#else
    return QStringLiteral("Enable desktop accessibility (AT-SPI) to identify the target application.");
#endif
}

QString restoreClipboardHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Restore the previous clipboard after Speecher pastes.");
#else
    return QStringLiteral("Restore the previous clipboard after virtual-keyboard paste.");
#endif
}

// The categories the Output page offers a paste rule for. A rule stored for any
// other category is left alone rather than dropped.
QList<AppCategory> managedPasteCategories()
{
    return {
        AppCategory::Terminal,
        AppCategory::Browser,
        AppCategory::Email,
        AppCategory::Office,
        AppCategory::CodeEditor,
        AppCategory::AiCoding,
        AppCategory::General,
    };
}

// A paste rule reads as a group of apps, where a recognition rule reads as one.
QString pasteCategoryLabel(AppCategory category)
{
    switch (category) {
    case AppCategory::Terminal:
        return QStringLiteral("Terminals");
    case AppCategory::Browser:
        return QStringLiteral("Browsers");
    case AppCategory::Email:
        return QStringLiteral("Email apps");
    case AppCategory::Office:
        return QStringLiteral("Office apps");
    case AppCategory::CodeEditor:
        return QStringLiteral("Code editors");
    case AppCategory::AiCoding:
        return QStringLiteral("AI coding apps");
    case AppCategory::General:
    case AppCategory::Unknown:
        break;
    }
    return QStringLiteral("Other apps");
}

void setPasteRule(QList<PasteRule> &rules,
                  PasteRuleScope scope,
                  const QString &match,
                  PasteMethod method)
{
    for (PasteRule &rule : rules) {
        if (rule.scope == scope && rule.match == match) {
            rule.method = method;
            rule.enabled = true;
            return;
        }
    }
    rules.append({scope, match, method, true});
}

void removePasteRule(QList<PasteRule> &rules, PasteRuleScope scope, const QString &match)
{
    rules.removeIf([scope, &match](const PasteRule &rule) {
        return rule.scope == scope && rule.match == match;
    });
}

QList<RowOption> appCategoryOptions()
{
    QList<RowOption> options{{QString(), QStringLiteral("Automatic")}};
    for (AppCategory category : {AppCategory::General,
                                 AppCategory::Terminal,
                                 AppCategory::Browser,
                                 AppCategory::Email,
                                 AppCategory::Office,
                                 AppCategory::CodeEditor,
                                 AppCategory::AiCoding}) {
        options.append({appCategoryName(category), appCategoryLabel(category)});
    }
    return options;
}

QList<RowOption> writingProfileOptions()
{
    QList<RowOption> options{{QString(), QStringLiteral("Automatic")}};
    for (WritingProfile profile : {WritingProfile::Work,
                                   WritingProfile::Email,
                                   WritingProfile::Personal,
                                   WritingProfile::AiCoding,
                                   WritingProfile::Other}) {
        options.append({writingProfileName(profile), writingProfileLabel(profile)});
    }
    return options;
}

Options fixedOptions(QList<RowOption> options)
{
    return [options = std::move(options)](const AppSettings &) { return options; };
}

SettingsRow choiceRow(QString id, QString label, QString help, Options options, Getter get, Setter set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Choice;
    row.options = std::move(options);
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toString());
    };
    return row;
}

SettingsRow toggleRow(QString id,
                      QString label,
                      QString help,
                      std::function<bool(const AppSettings &)> get,
                      std::function<void(AppSettings &, bool)> set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Toggle;
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toBool());
    };
    return row;
}

SettingsRow numberRow(QString id,
                      QString label,
                      QString help,
                      NumberRange range,
                      std::function<int(const AppSettings &)> get,
                      std::function<void(AppSettings &, int)> set)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Number;
    row.range = std::move(range);
    row.value = [get = std::move(get)](const AppSettings &settings) { return QVariant(get(settings)); };
    row.apply = [set = std::move(set)](AppSettings &settings, const QVariant &value) {
        set(settings, value.toInt());
    };
    return row;
}

SettingsRow actionRow(QString id, QString label, QString help, QString actionLabel)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Action;
    row.actionLabel = std::move(actionLabel);
    return row;
}

SettingsRow customRow(QString id, QString label, QString help)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Custom;
    return row;
}

SettingsRow collectionRow(QString id, QString label, QString help, CollectionDescriptor collection)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Collection;
    row.value = [collection](const AppSettings &settings) {
        return QVariant::fromValue(collection.records(settings));
    };
    row.apply = [collection](AppSettings &settings, const QVariant &value) {
        collection.apply(settings, value.value<QList<QVariantMap>>());
    };
    row.collection = std::move(collection);
    return row;
}

SettingsRow infoRow(QString id, QString label, QString help, QString text)
{
    SettingsRow row;
    row.id = std::move(id);
    row.label = std::move(label);
    row.help = std::move(help);
    row.kind = RowKind::Info;
    row.value = [text = std::move(text)](const AppSettings &) { return QVariant(text); };
    return row;
}

SettingsPage generalPage(const SchemaContext &context)
{
    SettingsRow theme = choiceRow(
        QStringLiteral("themeControl"),
        QStringLiteral("Theme"),
        QString(),
        fixedOptions({
            {QStringLiteral("system"), QStringLiteral("System")},
            {QStringLiteral("light"), QStringLiteral("Light")},
            {QStringLiteral("dark"), QStringLiteral("Dark")},
        }),
        [](const AppSettings &settings) { return settings.ui.theme; },
        [](AppSettings &settings, const QString &value) { settings.ui.theme = value; });

    return {
        QStringLiteral("general"),
        QStringLiteral("General"),
        QStringLiteral("preferences-system"),
        QStringLiteral("gearshape"),
        {
            {QStringLiteral("Appearance & behavior"),
             QString(),
             {
                 std::move(theme),
                 toggleRow(QStringLiteral("pauseMedia"),
                           QStringLiteral("Media"),
                           QStringLiteral("Pause playing media while dictating"),
                           [](const AppSettings &settings) { return settings.ui.pauseMediaDuringTranscription; },
                           [](AppSettings &settings, bool value) { settings.ui.pauseMediaDuringTranscription = value; }),
                 toggleRow(QStringLiteral("soundsEnabled"),
                           QStringLiteral("Sounds"),
                           QStringLiteral("Play sounds when dictation starts and stops"),
                           [](const AppSettings &settings) { return settings.ui.soundsEnabled; },
                           [](AppSettings &settings, bool value) { settings.ui.soundsEnabled = value; }),
                 numberRow(QStringLiteral("previewWords"),
                           QStringLiteral("Preview words"),
                           QStringLiteral("Trailing words shown in the popup."),
                           {1, 40, 1, QString()},
                           [](const AppSettings &settings) { return settings.ui.previewWords; },
                           [](AppSettings &settings, int value) { settings.ui.previewWords = value; }),
             }},
            {QStringLiteral("System"),
             QString(),
             {
                 infoRow(QStringLiteral("clipboardOutputStatus"),
                         QStringLiteral("Clipboard output"),
                         QStringLiteral("Current platform clipboard path."),
                         context.primaryOutputStatus),
             }},
            {QStringLiteral("Maintenance"),
             QString(),
             {
                 actionRow(QStringLiteral("runSetup"),
                           QStringLiteral("Setup assistant"),
                           QStringLiteral("Check sign-in, microphone, accessibility, delivery, and refinement again."),
                           QStringLiteral("Run setup assistant…")),
                 actionRow(QStringLiteral("openReleases"),
                           QStringLiteral("Updates"),
                           QStringLiteral("Updates are manual; open the GitHub releases page when you want to check."),
                           QStringLiteral("Open releases")),
             }},
        },
    };
}

SettingsPage audioPage(const SchemaContext &context)
{
    SettingsRow speechProvider = choiceRow(
        QStringLiteral("speechProvider"),
        QStringLiteral("Transcription"),
        QStringLiteral("Service used to turn speech into a Raw Transcript."),
        fixedOptions(context.speechProviders),
        [](const AppSettings &settings) { return settings.speech.providerId; },
        [](AppSettings &settings, const QString &value) { settings.speech.providerId = value; });
    speechProvider.contentWidthHint = 24;

    SettingsRow device = choiceRow(
        QStringLiteral("audioDevice"),
        QStringLiteral("Microphone"),
        QStringLiteral("Input device used for dictation."),
        [lister = context.audioInputDevices](const AppSettings &settings) {
            return audioDeviceOptions(lister ? lister() : QList<RowOption>(), settings.audio.deviceId);
        },
        [](const AppSettings &settings) { return settings.audio.deviceId; },
        [](AppSettings &settings, const QString &value) { settings.audio.deviceId = value; });
    device.tooltip = QStringLiteral("Microphone Speecher records from.");
    device.contentWidthHint = 28;
    device.expensive = true;

    SettingsRow captureMode = choiceRow(
        QStringLiteral("captureMode"),
        QStringLiteral("Capture mode"),
        QStringLiteral("Open the microphone only while listening, or keep it warm between captures."),
        fixedOptions({
            {QStringLiteral("on_demand"), QStringLiteral("On demand")},
            {QStringLiteral("warm"), QStringLiteral("Warm")},
        }),
        [](const AppSettings &settings) { return settings.audio.mode; },
        [](AppSettings &settings, const QString &value) { settings.audio.mode = value; });
    captureMode.tooltip = QStringLiteral(
        "Warm keeps the microphone stream open between captures for lower startup latency.");

    SettingsRow vadEnabled = toggleRow(
        QStringLiteral("vadEnabled"),
        QStringLiteral("Silence"),
        QStringLiteral("Trim leading and trailing silence"),
        [](const AppSettings &settings) { return settings.audio.vadEnabled; },
        [](AppSettings &settings, bool value) { settings.audio.vadEnabled = value; });
    vadEnabled.tooltip = QStringLiteral(
        "Suppress leading, trailing, and long in-between silence before audio is sent.");

    SettingsRow vadThreshold = numberRow(
        QStringLiteral("vadThresholdPercent"),
        QStringLiteral("Voice threshold"),
        QStringLiteral("RMS level required before VAD treats audio as speech."),
        {1, 20, 1, QStringLiteral("%")},
        [](const AppSettings &settings) { return settings.audio.vadThresholdPercent; },
        [](AppSettings &settings, int value) { settings.audio.vadThresholdPercent = value; });
    vadThreshold.enabled = [](const AppSettings &settings, const Capabilities &) {
        return settings.audio.vadEnabled;
    };

    return {
        QStringLiteral("audio"),
        QStringLiteral("Audio"),
        QStringLiteral("preferences-desktop-sound"),
        QStringLiteral("waveform"),
        {
            {QStringLiteral("Transcription"), QString(), {std::move(speechProvider)}},
            {QStringLiteral("Capture"),
             QString(),
             {
                 std::move(device),
                 std::move(captureMode),
                 numberRow(QStringLiteral("preRollMs"),
                           QStringLiteral("Pre-roll"),
                           QStringLiteral("Audio kept before speech or before a warm capture starts."),
                           {0, 1500, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.preRollMs; },
                           [](AppSettings &settings, int value) { settings.audio.preRollMs = value; }),
                 numberRow(QStringLiteral("postRollMs"),
                           QStringLiteral("Post-roll"),
                           QStringLiteral("Audio kept after stop or after speech falls quiet."),
                           {0, 1500, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.postRollMs; },
                           [](AppSettings &settings, int value) { settings.audio.postRollMs = value; }),
                 numberRow(QStringLiteral("readinessTimeoutMs"),
                           QStringLiteral("Readiness timeout"),
                           QStringLiteral("How long Speecher waits for the first microphone sample."),
                           {150, 3000, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.readinessTimeoutMs; },
                           [](AppSettings &settings, int value) { settings.audio.readinessTimeoutMs = value; }),
             }},
            {QStringLiteral("Silence trimming"),
             QString(),
             {std::move(vadEnabled), std::move(vadThreshold)}},
        },
    };
}

SettingsPage refinementPage(const SchemaContext &context)
{
    QList<RowOption> refiners = context.refinementProviders;
    refiners.append({QStringLiteral("none"), QStringLiteral("None")});

    SettingsRow targetContext = toggleRow(
        QStringLiteral("targetContextControl"),
        QStringLiteral("Context"),
        QStringLiteral("Send the target app's context to the refiner"),
        [](const AppSettings &settings) { return settings.refinement.useTargetContext; },
        [](AppSettings &settings, bool value) { settings.refinement.useTargetContext = value; });
    targetContext.disabledHelp =
        QStringLiteral("Enable desktop accessibility (AT-SPI) to use target-aware refinement.");
    targetContext.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.targetAccessibility;
    };

    SettingsRow screenshots = toggleRow(
        QStringLiteral("includeScreenshotContext"),
        QStringLiteral("Screenshots"),
        QStringLiteral("Allow screenshots as refinement context"),
        [](const AppSettings &settings) { return settings.refinement.includeScreenshotContext; },
        [](AppSettings &settings, bool value) { settings.refinement.includeScreenshotContext = value; });
    screenshots.tooltip = QStringLiteral(
        "Captured through the desktop portal and kept only for the current dictation.");
    screenshots.disabledHelp = QStringLiteral(
        "Choose an image-capable OpenAI or Anthropic refiner to send screenshot context.");
    screenshots.enabled = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.providerId == QStringLiteral("openai")
            || settings.refinement.providerId == QStringLiteral("anthropic");
    };

    SettingsRow profileBehavior;
    profileBehavior.id = QStringLiteral("writingProfileBehavior");
    profileBehavior.label = QStringLiteral("Profile behavior");
    profileBehavior.help = QStringLiteral(
        "Choose cleanup strength and an optional explicit tone for each automatically detected profile.");
    profileBehavior.kind = RowKind::Custom;
    profileBehavior.value = [](const AppSettings &settings) {
        return QVariant::fromValue(settings.refinement.writingProfiles);
    };
    profileBehavior.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.writingProfiles = value.value<QList<WritingProfileSettings>>();
    };

    return {
        QStringLiteral("refinement"),
        QStringLiteral("Refinement"),
        QStringLiteral("tools-wizard"),
        QStringLiteral("wand.and.stars"),
        {
            {QStringLiteral("Refinement"),
             QString(),
             {
                 choiceRow(QStringLiteral("refinementProvider"),
                           QStringLiteral("Refinement"),
                           QStringLiteral("Clean up dictated text after capture."),
                           fixedOptions(refiners),
                           [](const AppSettings &settings) { return settings.refinement.providerId; },
                           [](AppSettings &settings, const QString &value) { settings.refinement.providerId = value; }),
                 choiceRow(QStringLiteral("defaultWritingProfile"),
                           QStringLiteral("Fallback profile"),
                           QStringLiteral("Writing profile used when the target app does not imply one."),
                           fixedOptions({
                               {QStringLiteral("work"), QStringLiteral("Work")},
                               {QStringLiteral("email"), QStringLiteral("Email")},
                               {QStringLiteral("personal"), QStringLiteral("Personal")},
                               {QStringLiteral("ai_coding"), QStringLiteral("AI coding")},
                               {QStringLiteral("other"), QStringLiteral("Other")},
                           }),
                           [](const AppSettings &settings) { return settings.refinement.defaultWritingProfile; },
                           [](AppSettings &settings, const QString &value) { settings.refinement.defaultWritingProfile = value; }),
                 std::move(targetContext),
                 std::move(screenshots),
             }},
            {QStringLiteral("Prompt shaping"), QString(), {std::move(profileBehavior)}},
        },
    };
}

// The Writing Profile overrides a recognition rule replaced are folded in on
// read and dropped on write, so opening the page once retires them.
QList<QVariantMap> recognitionRecords(const AppSettings &settings)
{
    QList<QVariantMap> records;
    const auto append = [&records](const AppRecognitionRule &rule, const QString &source) {
        records.append({
            {kMatchColumn, rule.match},
            {kCategoryColumn, rule.category ? appCategoryName(*rule.category) : QString()},
            {kProfileColumn, rule.writingProfile ? writingProfileName(*rule.writingProfile) : QString()},
            {kSourceColumn, source},
        });
    };
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        append(rule, QStringLiteral("Built-in"));
    }
    for (const AppRecognitionRule &rule : recognitionRulesWithMigratedProfileOverrides(
             settings.appRecognitionRules, settings.refinement.writingProfileOverrides)) {
        append(rule, QStringLiteral("Custom"));
    }
    return records;
}

QList<AppRecognitionRule> recognitionRules(const QList<QVariantMap> &records)
{
    QList<AppRecognitionRule> rules;
    for (const QVariantMap &record : records) {
        AppRecognitionRule rule;
        rule.match = record.value(kMatchColumn).toString().trimmed();
        const QString category = record.value(kCategoryColumn).toString();
        const QString profile = record.value(kProfileColumn).toString();
        if (!category.isEmpty()) {
            rule.category = appCategoryFromName(category);
        }
        if (!profile.isEmpty()) {
            rule.writingProfile = writingProfileFromName(profile);
        }
        if (!rule.match.isEmpty() && (rule.category || rule.writingProfile)) {
            rules.append(rule);
        }
    }
    return rules;
}

SettingsPage applicationsPage()
{
    CollectionDescriptor rules;
    rules.columns = {
        {kMatchColumn,
         QStringLiteral("Application ID or name contains"),
         ColumnKind::Text,
         {},
         true,
         QStringLiteral("Matches the application ID, application name, process name, or accessible role.")},
        {kCategoryColumn, QStringLiteral("App type"), ColumnKind::Choice, appCategoryOptions},
        {kProfileColumn, QStringLiteral("Writing profile"), ColumnKind::Choice, writingProfileOptions},
        {kSourceColumn, QStringLiteral("Source"), ColumnKind::ReadOnly},
    };
    rules.records = recognitionRecords;
    rules.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.appRecognitionRules = recognitionRules(records);
        settings.refinement.writingProfileOverrides.clear();
    };
    rules.blankRecord = {{kSourceColumn, QStringLiteral("Custom")}};
    rules.lockedRecordCount = [] { return int(builtInAppRecognitionRules().size()); };
    rules.addLabel = QStringLiteral("Add application");
    rules.minimumHeight = 320;

    SettingsRow row = collectionRow(
        QStringLiteral("appRecognitionRules"),
        QString(),
        QStringLiteral("Built-in matches are read-only. Custom matches take priority and can set the "
                       "app type used for paste rules, the Writing Profile used for refinement, or both."),
        std::move(rules));
    row.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.targetAccessibility;
    };
    row.disabledHelp =
        QStringLiteral("Enable desktop accessibility (AT-SPI) to identify target applications.");

    return {
        QStringLiteral("applications"),
        QStringLiteral("Applications"),
        QStringLiteral("preferences-desktop-default-applications"),
        QStringLiteral("square.grid.2x2"),
        {{QStringLiteral("Application recognition"), QString(), {std::move(row)}}},
    };
}

QList<PasteRule> applicationPasteRules(const QList<QVariantMap> &records)
{
    QList<PasteRule> rules;
    for (const QVariantMap &record : records) {
        const QString applicationId = record.value(kApplicationColumn).toString().trimmed();
        if (applicationId.isEmpty()) {
            continue;
        }
        rules.append({PasteRuleScope::Application,
                      applicationId,
                      pasteMethodFromName(record.value(kMethodColumn).toString()),
                      record.value(kEnabledColumn).toBool()});
    }
    return rules;
}

SettingsRow applicationPasteRuleRow()
{
    CollectionDescriptor descriptor;
    descriptor.columns = {
        {kEnabledColumn, QStringLiteral("Enabled"), ColumnKind::Toggle},
        {kApplicationColumn,
         QStringLiteral("Application ID"),
         ColumnKind::Text,
         {},
         true,
         applicationIdHint()},
        {kMethodColumn,
         QStringLiteral("Paste behavior"),
         ColumnKind::Choice,
         [] { return pasteMethodOptions(true, false); }},
    };
    descriptor.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        for (const PasteRule &rule : settings.output.pasteRules) {
            if (rule.scope == PasteRuleScope::Application) {
                records.append({{kEnabledColumn, rule.enabled},
                                {kApplicationColumn, rule.match},
                                {kMethodColumn, pasteMethodName(rule.method)}});
            }
        }
        return records;
    };
    descriptor.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        QList<PasteRule> kept;
        for (const PasteRule &rule : settings.output.pasteRules) {
            if (rule.scope != PasteRuleScope::Application) {
                kept.append(rule);
            }
        }
        settings.output.pasteRules = applicationPasteRules(records) + kept;
    };
    descriptor.blankRecord = {{kEnabledColumn, true},
                              {kApplicationColumn, QString()},
                              {kMethodColumn, pasteMethodName(PasteMethod::StandardPaste)}};
    descriptor.validate = [](const QList<QVariantMap> &records) {
        return validatePasteRules(applicationPasteRules(records));
    };
    descriptor.addLabel = QStringLiteral("Add rule");
    descriptor.minimumHeight = 150;

    SettingsRow row = collectionRow(QStringLiteral("applicationPasteRules"),
                                    QStringLiteral("App-specific paste rules"),
                                    applicationPasteRuleHint(),
                                    std::move(descriptor));
    return row;
}

SettingsRow categoryPasteRuleRow(AppCategory category)
{
    const QString match = appCategoryName(category);
    return choiceRow(
        QStringLiteral("categoryPasteRule_") + match,
        pasteCategoryLabel(category),
        QStringLiteral("Override the fallback for this application category."),
        fixedOptions(pasteMethodOptions(false, true)),
        [match](const AppSettings &settings) {
            for (const PasteRule &rule : settings.output.pasteRules) {
                if (rule.scope == PasteRuleScope::Category && rule.match == match) {
                    return pasteMethodName(rule.method);
                }
            }
            return inheritGlobalPasteRule();
        },
        [match](AppSettings &settings, const QString &value) {
            if (value == inheritGlobalPasteRule()) {
                removePasteRule(settings.output.pasteRules, PasteRuleScope::Category, match);
                return;
            }
            setPasteRule(settings.output.pasteRules,
                         PasteRuleScope::Category,
                         match,
                         pasteMethodFromName(value));
        });
}

SettingsPage outputPage(const SchemaContext &context)
{
    SettingsRow method = customRow(QStringLiteral("outputMethod"),
                                   QStringLiteral("Method"),
                                   QStringLiteral("How Speecher delivers final text."));
    method.value = [](const AppSettings &settings) { return QVariant(settings.output.method); };
    method.apply = [](AppSettings &settings, const QVariant &value) {
        settings.output.method = value.toString();
    };

    SettingsRow restoreClipboard = toggleRow(
        QStringLiteral("restoreClipboardAfterTyping"),
        QStringLiteral("Clipboard"),
        QStringLiteral("Restore previous clipboard contents after typing"),
        [](const AppSettings &settings) { return settings.output.restoreClipboardAfterTyping; },
        [](AppSettings &settings, bool value) { settings.output.restoreClipboardAfterTyping = value; });
    restoreClipboard.tooltip = restoreClipboardHint();

    QList<SettingsRow> pasteRows{choiceRow(
        QStringLiteral("globalPasteRule"),
        QStringLiteral("Global fallback"),
        QStringLiteral("Paste behavior used unless a category or exact-app rule overrides it."),
        fixedOptions(pasteMethodOptions(false, false)),
        [](const AppSettings &settings) {
            for (const PasteRule &rule : settings.output.pasteRules) {
                if (rule.scope == PasteRuleScope::Global) {
                    return pasteMethodName(rule.method);
                }
            }
            return pasteMethodName(PasteMethod::StandardPaste);
        },
        [](AppSettings &settings, const QString &value) {
            setPasteRule(settings.output.pasteRules,
                         PasteRuleScope::Global,
                         QString(),
                         pasteMethodFromName(value));
        })};
    for (AppCategory category : managedPasteCategories()) {
        pasteRows.append(categoryPasteRuleRow(category));
    }
    pasteRows.append(applicationPasteRuleRow());
    // Every row below the global fallback needs a known target application, so
    // they stand or fall together with desktop accessibility.
    for (int index = 1; index < pasteRows.size(); ++index) {
        pasteRows[index].groupId = QStringLiteral("targetPasteControls");
        pasteRows[index].enabled = [](const AppSettings &, const Capabilities &capabilities) {
            return capabilities.targetAccessibility;
        };
        pasteRows[index].disabledHelp = targetAccessibilityHint();
    }

    QList<SettingsSection> sections{
        {QStringLiteral("Delivery"),
         QString(),
         {
             std::move(method),
             choiceRow(QStringLiteral("outputFormat"),
                       QStringLiteral("Format"),
                       QStringLiteral("Default clipboard representation. A CLI shortcut can override "
                                      "this per dictation."),
                       fixedOptions({
                           {QStringLiteral("plain"), QStringLiteral("Plain text")},
                           {QStringLiteral("html"), QStringLiteral("HTML and plain text")},
                       }),
                       [](const AppSettings &settings) { return outputFormatName(settings.output.format); },
                       [](AppSettings &settings, const QString &value) {
                           settings.output.format = outputFormatFromString(value);
                       }),
             numberRow(QStringLiteral("completionStatusDuration"),
                       QStringLiteral("Status duration"),
                       QStringLiteral("How long the completed delivery result stays visible."),
                       {0, 5000, 50, QStringLiteral(" ms")},
                       [](const AppSettings &settings) { return settings.output.completionStatusDurationMs; },
                       [](AppSettings &settings, int value) {
                           settings.output.completionStatusDurationMs = value;
                       }),
             std::move(restoreClipboard),
         }},
        {QStringLiteral("Paste behavior"), QString(), pasteRows},
    };
    if (context.virtualKeyboardSetup) {
        sections.append({QStringLiteral("Advanced"),
                         QString(),
                         {customRow(QStringLiteral("virtualKeyboard"),
                                    QStringLiteral("Virtual keyboard"),
                                    QString())}});
    }

    return {
        QStringLiteral("output"),
        QStringLiteral("Output"),
        QStringLiteral("klipper"),
        QStringLiteral("doc.on.clipboard"),
        sections,
    };
}

} // namespace

const SettingsPage &SettingsSchema::page(const QString &id) const
{
    for (const SettingsPage &candidate : pages) {
        if (candidate.id == id) {
            return candidate;
        }
    }
    qFatal("no settings page with id %s", qPrintable(id));
}

QList<RowOption> audioDeviceOptions(const QList<RowOption> &devices, const QString &selectedDeviceId)
{
    const RowOption missing{selectedDeviceId,
                            QStringLiteral("Missing microphone"),
                            QStringLiteral("This saved microphone is not currently available."),
                            false};
    if (devices.isEmpty()) {
        QList<RowOption> options{{QString(),
                                  QStringLiteral("No microphones found"),
                                  QStringLiteral("Connect or enable an input device, then try again."),
                                  false}};
        if (!selectedDeviceId.isEmpty()) {
            options.append(missing);
        }
        return options;
    }

    QList<RowOption> options{{QString(), QStringLiteral("System default")}};
    bool selectedFound = selectedDeviceId.isEmpty();
    for (const RowOption &device : devices) {
        options.append(device);
        selectedFound = selectedFound || device.id == selectedDeviceId;
    }
    if (!selectedFound) {
        options.append(missing);
    }
    return options;
}

SettingsSchema buildSettingsSchema(const SchemaContext &context)
{
    return {{generalPage(context),
             audioPage(context),
             applicationsPage(),
             outputPage(context),
             refinementPage(context)}};
}

} // namespace speecher
