#include "core/settings/SettingsSchema.h"

namespace speecher {

namespace {

using Getter = std::function<QString(const AppSettings &)>;
using Setter = std::function<void(AppSettings &, const QString &)>;
using Options = std::function<QList<RowOption>(const AppSettings &)>;

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
    return {{generalPage(context), audioPage(context), refinementPage(context)}};
}

} // namespace speecher
