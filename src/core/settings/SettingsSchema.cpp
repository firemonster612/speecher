#include "core/settings/SettingsSchema.h"

#include "core/BindingProcessor.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

static void initializeReleaseNotesResource()
{
    Q_INIT_RESOURCE(release_notes);
}

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
const QString kStarColumn = QStringLiteral("starred");
const QString kTermColumn = QStringLiteral("term");
const QString kUsesColumn = QStringLiteral("uses");
const QString kLastUsedColumn = QStringLiteral("lastUsed");
const QString kHeardColumn = QStringLiteral("original");
const QString kCorrectedColumn = QStringLiteral("corrected");
const QString kCorrectedAppColumn = QStringLiteral("applicationId");
const QString kPhraseColumn = QStringLiteral("phrase");
const QString kReplacementColumn = QStringLiteral("replacement");

// Keys no column names: the editor carries them from record to record so a
// value nobody can see survives an edit to one that everybody can.
const QString kLastUsedMsKey = QStringLiteral("lastUsedMs");
const QString kCorrectionIdKey = QStringLiteral("id");
const QString kCreatedAtKey = QStringLiteral("createdAtMs");
const QString kConfidenceKey = QStringLiteral("confidence");
const QString kEvidenceCountKey = QStringLiteral("evidenceCount");
const QString kLastObservedKey = QStringLiteral("lastObservedAtMs");
const QString kWhatsNewAction = QStringLiteral("whatsNew");
const QString kWhatsNewNotes = QStringLiteral("whatsNewNotes");

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

QString nightlyChangesLink(const QString &lastVersion, const QString &currentVersion)
{
    if (!currentVersion.contains(QStringLiteral("-nightly"))) {
        return {};
    }
    const QRegularExpression sha(QStringLiteral("[+]g([0-9a-fA-F]+)$"));
    const QRegularExpressionMatch last = sha.match(lastVersion);
    const QRegularExpressionMatch current = sha.match(currentVersion);
    if (last.hasMatch() && current.hasMatch()) {
        return QStringLiteral("[Compare commits](https://github.com/firemonster612/speecher/compare/%1...%2)")
            .arg(last.captured(1), current.captured(1));
    }
    return QStringLiteral("[View releases](https://github.com/firemonster612/speecher/releases)");
}

QString releaseNotesMarkdown(const SchemaContext &context)
{
    static const bool resourceInitialized = [] {
        initializeReleaseNotesResource();
        return true;
    }();
    Q_UNUSED(resourceInitialized);
    struct Note {
        QString version;
        QString body;
    };
    QList<Note> notes;
    const QDir directory(QStringLiteral(":/releases"));
    for (const QString &fileName : directory.entryList({QStringLiteral("*.md")}, QDir::Files)) {
        QFile file(directory.filePath(fileName));
        if (file.open(QIODevice::ReadOnly)) {
            notes.append({QFileInfo(fileName).completeBaseName(),
                          QString::fromUtf8(file.readAll()).trimmed()});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const Note &left, const Note &right) {
        return compareBaseVersions(left.version, right.version) > 0;
    });

    QList<Note> selected;
    if (!context.lastSeenVersion.isEmpty()) {
        for (const Note &note : notes) {
            if (compareBaseVersions(note.version, context.lastSeenVersion) > 0
                && compareBaseVersions(note.version, context.currentVersion) <= 0) {
                selected.append(note);
            }
        }
    }
    if (selected.isEmpty() && !notes.isEmpty()) {
        selected.append(notes.first());
    }

    QString markdown;
    for (const Note &note : selected) {
        if (!markdown.isEmpty()) {
            markdown += QStringLiteral("\n\n---\n\n");
        }
        markdown += QStringLiteral("# Speecher %1\n\n%2").arg(note.version, note.body);
    }
    if (markdown.isEmpty()) {
        markdown = QStringLiteral("Release notes are not available in this build.");
    }
    const QString changes = nightlyChangesLink(context.lastSeenVersion, context.currentVersion);
    if (!changes.isEmpty()) {
        markdown += QStringLiteral("\n\n%1").arg(changes);
    }
    return markdown;
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

    QList<SettingsRow> systemRows;
#ifdef Q_OS_MACOS
    systemRows.append(toggleRow(
        QStringLiteral("launchAtLogin"),
        QStringLiteral("Start Speecher at login"),
        QStringLiteral("Dictation only works while Speecher is running."),
        [](const AppSettings &settings) { return settings.launchAtLogin; },
        [](AppSettings &settings, bool value) { settings.launchAtLogin = value; }));
#endif
#ifdef Q_OS_LINUX
    systemRows.append(customRow(
        QStringLiteral("globalShortcut"),
        QStringLiteral("Global Shortcut"),
        QStringLiteral("Start or stop dictation from anywhere.")));
#endif
    systemRows.append(infoRow(QStringLiteral("clipboardOutputStatus"),
                              QStringLiteral("Clipboard output"),
                              QStringLiteral("Current platform clipboard path."),
                              context.primaryOutputStatus));

    SettingsRow updateChannel = choiceRow(
        QStringLiteral("updateChannel"),
        QStringLiteral("Update channel"),
        QString(),
        fixedOptions({
            {QStringLiteral("stable"),
             QStringLiteral("Stable Release"),
             QStringLiteral("Hand-tested releases for general use.")},
            {QStringLiteral("nightly"),
             QStringLiteral("Nightly Build"),
             QStringLiteral("Republished automatically from every push to master.")},
        }),
        [](const AppSettings &settings) { return updateChannelName(settings.updates.channel); },
        [](AppSettings &settings, const QString &value) {
            settings.updates.channel = updateChannelFromName(value);
        });
    updateChannel.sinceVersion = QStringLiteral("0.2.0");
    SettingsRow autoCheck = toggleRow(
        QStringLiteral("autoCheckUpdates"),
        QStringLiteral("Check for updates automatically"),
        QStringLiteral("Check the selected Update Channel at startup and once a day."),
        [](const AppSettings &settings) { return settings.updates.autoCheck; },
        [](AppSettings &settings, bool value) { settings.updates.autoCheck = value; });
    autoCheck.sinceVersion = QStringLiteral("0.2.0");
    SettingsRow autoInstall = toggleRow(
        QStringLiteral("autoInstallUpdates"),
        QStringLiteral("Download and install updates automatically"),
        QStringLiteral("AppImage updates install in the background and take effect after restart."),
        [](const AppSettings &settings) { return settings.updates.autoInstall; },
        [](AppSettings &settings, bool value) { settings.updates.autoInstall = value; });
    autoInstall.sinceVersion = QStringLiteral("0.2.0");
    autoInstall.visible = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.isAppImage;
    };

    SettingsPage page{
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
             std::move(systemRows)},
            {QStringLiteral("Maintenance"),
             QString(),
             {
                 actionRow(QStringLiteral("runSetup"),
                           QStringLiteral("Setup assistant"),
                           QStringLiteral("Check sign-in, microphone, accessibility, delivery, and refinement again."),
                           QStringLiteral("Run setup assistant…")),
             }},
            {QStringLiteral("Updates"),
             QString(),
             {
                 std::move(updateChannel),
                 std::move(autoCheck),
                 std::move(autoInstall),
                 actionRow(QStringLiteral("checkForUpdates"),
                           QStringLiteral("Check for updates"),
                           QStringLiteral("Check the selected Update Channel for a newer build."),
                           QStringLiteral("Check now")),
                 infoRow(QStringLiteral("currentVersion"),
                         QStringLiteral("Current version"),
                         QString(),
                         context.currentVersion.isEmpty()
                             ? QStringLiteral("Unknown")
                             : context.currentVersion),
                 actionRow(kWhatsNewAction,
                           QStringLiteral("What's New"),
                           QStringLiteral("Release notes for this version, and the settings it added."),
                           QStringLiteral("What's New…")),
             }},
        },
    };
    return page;
}

SettingsPage whatsNewPage(const QList<SettingsPage> &pages, const SchemaContext &context)
{
    QList<SettingsRow> newRows;
    if (!context.lastSeenVersion.isEmpty()) {
        for (const SettingsPage &page : pages) {
            for (const SettingsSection &section : page.sections) {
                for (const SettingsRow &row : section.rows) {
                    // Custom rows need a page-specific factory, which What's New does not have.
                    if (row.kind != RowKind::Custom
                        && !row.sinceVersion.isEmpty()
                        && compareBaseVersions(row.sinceVersion, context.lastSeenVersion) > 0
                        && compareBaseVersions(row.sinceVersion, context.currentVersion) <= 0) {
                        newRows.append(row);
                    }
                }
            }
        }
    }

    SettingsRow notes = customRow(kWhatsNewNotes, QStringLiteral("Release notes"), QString());
    const QString markdown = releaseNotesMarkdown(context);
    notes.value = [markdown](const AppSettings &) { return QVariant(markdown); };
    QList<SettingsSection> sections{{QString(), QString(), {std::move(notes)}}};
    if (!newRows.isEmpty()) {
        sections.append({QStringLiteral("Try the new settings"), QString(), std::move(newRows)});
    }
    return {QStringLiteral("whatsNew"),
            QStringLiteral("What's New"),
            QStringLiteral("help-about"),
            QStringLiteral("sparkles"),
            std::move(sections)};
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
    QList<RowOption> refiners;
    for (const RefinementProvider &provider : context.refinementProviders) {
        refiners.append({provider.id, provider.label});
    }
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
    screenshots.enabled = [providers = context.refinementProviders](const AppSettings &settings,
                                                                   const Capabilities &) {
        for (const RefinementProvider &provider : providers) {
            if (provider.id == settings.refinement.providerId) {
                return provider.supportsScreenshotContext;
            }
        }
        return false;
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
        QStringLiteral("wand.and.sparkles"),
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

QString lastUsedLabel(qint64 lastUsedMs)
{
    return lastUsedMs > 0
        ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(lastUsedMs), QLocale::ShortFormat)
        : QStringLiteral("Never");
}

QList<QVariantMap> vocabularyRecords(const QList<VocabularyEntry> &entries)
{
    QList<QVariantMap> records;
    records.reserve(entries.size());
    for (const VocabularyEntry &entry : entries) {
        records.append({
            {kStarColumn, entry.starred},
            {kTermColumn, entry.term},
            {kSourceColumn, entry.source.isEmpty() ? QStringLiteral("manual") : entry.source},
            {kUsesColumn, qMax(0, entry.frequency)},
            {kLastUsedColumn, lastUsedLabel(entry.lastUsedMs)},
            {kLastUsedMsKey, entry.lastUsedMs},
        });
    }
    return records;
}

QList<VocabularyEntry> vocabularyEntries(const QList<QVariantMap> &records)
{
    QList<VocabularyEntry> entries;
    entries.reserve(records.size());
    for (const QVariantMap &record : records) {
        const QString term = record.value(kTermColumn).toString();
        if (term.trimmed().isEmpty()) {
            continue;
        }
        entries.append({term,
                        record.value(kSourceColumn).toString(),
                        record.value(kStarColumn).toBool(),
                        record.value(kUsesColumn).toInt(),
                        record.value(kLastUsedMsKey).toLongLong()});
    }
    return normalizeVocabularyEntries(entries);
}

QStringList vocabularyTerms(const QList<VocabularyEntry> &entries)
{
    QStringList terms;
    terms.reserve(entries.size());
    for (const VocabularyEntry &entry : entries) {
        terms.append(entry.term);
    }
    return terms;
}

SettingsPage vocabularyPage()
{
    CollectionDescriptor terms;
    terms.columns = {
        {kStarColumn, QStringLiteral("Star"), ColumnKind::Toggle},
        {kTermColumn, QStringLiteral("Term"), ColumnKind::Text, {}, true},
        {kSourceColumn, QStringLiteral("Source"), ColumnKind::Text},
        {kUsesColumn, QStringLiteral("Uses"), ColumnKind::ReadOnly},
        {kLastUsedColumn, QStringLiteral("Last used"), ColumnKind::ReadOnly},
    };
    terms.records = [](const AppSettings &settings) {
        return vocabularyRecords(normalizeVocabularyEntries(settings.vocabulary));
    };
    // Normalising here rather than on every cell edit is what lets a person
    // finish typing a term that momentarily duplicates another one.
    terms.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.vocabulary = vocabularyEntries(records);
    };
    terms.blankRecord = {{kStarColumn, false},
                         {kTermColumn, QString()},
                         {kSourceColumn, QStringLiteral("manual")},
                         {kUsesColumn, 0},
                         {kLastUsedColumn, lastUsedLabel(0)},
                         {kLastUsedMsKey, qint64(0)}};
    terms.addLabel = QStringLiteral("Add");
    terms.supportsImport = {
        QStringLiteral("Import CSV"),
        QStringLiteral("CSV files (*.csv);;All files (*)"),
        QStringLiteral("Vocabulary not imported"),
        [](const QByteArray &csv, QString *error) {
            return vocabularyRecords(parseVocabularyCsv(csv, error));
        },
    };
    // These three are the whole page they sit on, so they get the editor a
    // page can hold rather than the one an embedded table needs.
    terms.minimumHeight = 320;

    SettingsRow limit;
    limit.id = QStringLiteral("vocabularyLimit");
    limit.label = QStringLiteral("Limit");
    limit.kind = RowKind::Info;
    limit.value = [](const AppSettings &settings) {
        return QVariant(VocabularyLimit::summary(
            vocabularyTerms(normalizeVocabularyEntries(settings.vocabulary))));
    };

    return {
        QStringLiteral("vocabulary"),
        QStringLiteral("Vocabulary"),
        QStringLiteral("accessories-dictionary"),
        QStringLiteral("character.book.closed"),
        {{QString(),
          QString(),
          {
              collectionRow(QStringLiteral("vocabularyEntries"),
                            QStringLiteral("Extra vocabulary"),
                            QStringLiteral("One term per line. Claude voice uses Deepgram Nova-3 "
                                           "keyterms: 500 tokens and 100 keyterms maximum."),
                            std::move(terms)),
              std::move(limit),
          }}},
    };
}

QList<LearnedCorrection> learnedCorrections(const QList<QVariantMap> &records)
{
    QList<LearnedCorrection> corrections;
    corrections.reserve(records.size());
    for (const QVariantMap &record : records) {
        LearnedCorrection correction;
        correction.id = record.value(kCorrectionIdKey).toString();
        correction.original = record.value(kHeardColumn).toString().trimmed();
        correction.corrected = record.value(kCorrectedColumn).toString().trimmed();
        correction.applicationId = record.value(kCorrectedAppColumn).toString().trimmed();
        correction.createdAtMs = record.value(kCreatedAtKey).toLongLong();
        correction.confidence = record.value(kConfidenceKey).toDouble();
        correction.enabled = record.value(kEnabledColumn).toBool();
        correction.evidenceCount = record.value(kEvidenceCountKey).toInt();
        correction.lastObservedAtMs = record.value(kLastObservedKey).toLongLong();
        if (!correction.id.isEmpty() && !correction.original.isEmpty()
            && !correction.corrected.isEmpty()) {
            corrections.append(correction);
        }
    }
    return corrections;
}

SettingsPage correctionsPage()
{
    SettingsRow learn = toggleRow(
        QStringLiteral("correctionLearningControl"),
        QStringLiteral("Learn corrections"),
        QString(),
        [](const AppSettings &settings) { return settings.correctionLearningEnabled; },
        [](AppSettings &settings, bool value) { settings.correctionLearningEnabled = value; });
    learn.tooltip = QStringLiteral("Observe a verified inserted span briefly and automatically "
                                   "learn high-confidence or repeated corrections.");
    learn.disabledHelp =
        QStringLiteral("Enable desktop accessibility (AT-SPI) to learn corrections after insertion.");
    learn.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.targetAccessibility;
    };

    CollectionDescriptor corrections;
    corrections.columns = {
        {kEnabledColumn, QStringLiteral("Enabled"), ColumnKind::Toggle},
        {kHeardColumn, QStringLiteral("Heard"), ColumnKind::Text, {}, true},
        {kCorrectedColumn, QStringLiteral("Corrected"), ColumnKind::Text, {}, true},
        {kCorrectedAppColumn,
         QStringLiteral("App"),
         ColumnKind::ReadOnly,
         {},
         false,
         QString(),
         [](const QVariantMap &record) {
             return QStringLiteral("Learned automatically · confidence %1%")
                 .arg(qRound(record.value(kConfidenceKey).toDouble() * 100.0));
         }},
    };
    corrections.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        records.reserve(settings.learnedCorrections.size());
        for (const LearnedCorrection &correction : settings.learnedCorrections) {
            records.append({
                {kEnabledColumn, correction.enabled},
                {kHeardColumn, correction.original},
                {kCorrectedColumn, correction.corrected},
                {kCorrectedAppColumn, correction.applicationId},
                {kCorrectionIdKey, correction.id},
                {kCreatedAtKey, correction.createdAtMs},
                {kConfidenceKey, correction.confidence},
                {kEvidenceCountKey, correction.evidenceCount},
                {kLastObservedKey, correction.lastObservedAtMs},
            });
        }
        return records;
    };
    corrections.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.learnedCorrections = learnedCorrections(records);
    };
    // Corrections arrive from watching an edit, so there is nothing to add here.
    corrections.actions = {
        {QStringLiteral("undoLatestLearn"), QStringLiteral("Undo latest learn")},
        {QStringLiteral("undoDelete"), QStringLiteral("Undo delete")},
    };
    corrections.minimumHeight = 320;

    return {
        QStringLiteral("corrections"),
        QStringLiteral("Learned corrections"),
        QStringLiteral("tools-check-spelling"),
        QStringLiteral("checkmark.bubble"),
        {{QString(),
          QString(),
          {
              std::move(learn),
              collectionRow(QStringLiteral("learnedCorrections"),
                            QString(),
                            QStringLiteral("Source-marked corrections learned after verified "
                                           "insertion. Edit, disable, delete, or undo deletions here."),
                            std::move(corrections)),
          }}},
    };
}

QList<BindingRule> bindingRules(const QList<QVariantMap> &records)
{
    QList<BindingRule> rules;
    rules.reserve(records.size());
    for (const QVariantMap &record : records) {
        rules.append({record.value(kPhraseColumn).toString(),
                      record.value(kReplacementColumn).toString()});
    }
    return rules;
}

QList<QVariantMap> bindingRecords(const QList<BindingRule> &rules)
{
    QList<QVariantMap> records;
    records.reserve(rules.size());
    for (const BindingRule &rule : rules) {
        records.append({{kPhraseColumn, rule.phrase}, {kReplacementColumn, rule.replacement}});
    }
    return records;
}

SettingsPage bindingsPage()
{
    CollectionDescriptor replacements;
    replacements.columns = {
        {kPhraseColumn, QStringLiteral("Spoken phrase"), ColumnKind::Text},
        {kReplacementColumn,
         QStringLiteral("Exact replacement or snippet"),
         ColumnKind::Text,
         {},
         true},
    };
    replacements.records = [](const AppSettings &settings) {
        return bindingRecords(settings.bindings);
    };
    replacements.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        settings.bindings = BindingProcessor::validateRules(bindingRules(records)).rules;
    };
    replacements.validate = [](const QList<QVariantMap> &records) {
        return BindingProcessor::validateRules(bindingRules(records)).messages();
    };
    replacements.blankRecord = {{kPhraseColumn, QString()}, {kReplacementColumn, QString()}};
    replacements.addLabel = QStringLiteral("Add replacement");
    replacements.supportsImport = {
        QStringLiteral("Import snippets JSON"),
        QStringLiteral("JSON files (*.json);;All files (*)"),
        QStringLiteral("Snippets not imported"),
        [](const QByteArray &json, QString *error) {
            return bindingRecords(BindingProcessor::parseJsonImport(json, error));
        },
    };
    replacements.minimumHeight = 320;

    return {
        QStringLiteral("bindings"),
        QStringLiteral("Replacements & snippets"),
        QStringLiteral("edit-find-replace"),
        QStringLiteral("arrow.left.arrow.right"),
        {{QString(),
          QString(),
          {collectionRow(QStringLiteral("bindingRules"),
                         QStringLiteral("Replacements & snippets"),
                         QStringLiteral("Replace a spoken phrase with exact text, including "
                                        "multi-line snippets. Matching ignores case and treats "
                                        "punctuation as spaces."),
                         std::move(replacements))}}},
    };
}

// What one refinement account contributes to the Providers page. A third
// provider is another entry in providerAccounts() plus the two AppSettings
// fields it names, rather than a third hand-written card.
struct ProviderAccount {
    QString sectionTitle;
    // A closing note under the card.
    QString note;
    QString modelRowId;
    QString modelLabel;
    QString modelHelp;
    QString modelTooltip;
    int modelWidthHint = 0;
    QList<RowOption> models;
    QString RefinementSettings::*model;
    // Said only while the chosen model's id contains this, which is how a model
    // that reads a transcript as instructions warns about it.
    QString cautionWhenModelContains;
    QString caution;
    QString effortRowId;
    QString effortLabel;
    QString effortHelp;
    QString effortTooltip;
    QList<RowOption> efforts;
    QString RefinementSettings::*effort;
    QString fastModeRowId;
    QString fastModeHelp;
    QString fastModeTooltip;
    bool RefinementSettings::*fastMode;
    // Where the credentials come from is a question for a keyring rather than a
    // value in AppSettings, so every front end answers it its own way.
    QList<SettingsRow> authRows;
};

// The account picker for a provider is only worth showing while its credentials
// come from CLI Proxy API.
const QString kCliProxyAuthMode = QStringLiteral("cliproxy");

QList<RowOption> namedModels(const QStringList &ids)
{
    QList<RowOption> models;
    models.reserve(ids.size());
    for (const QString &id : ids) {
        models.append({id, id});
    }
    return models;
}

QList<ProviderAccount> providerAccounts()
{
    ProviderAccount openAi;
    openAi.sectionTitle = QStringLiteral("OpenAI account");
    openAi.modelRowId = QStringLiteral("openAiModel");
    openAi.modelLabel = QStringLiteral("OpenAI model");
    openAi.modelHelp = QStringLiteral("Model used for refinement.");
    openAi.modelTooltip = QStringLiteral("Defaults to gpt-5.6-luna with no reasoning effort. "
                                         "Select another model or type another model ID.");
    openAi.modelWidthHint = 16;
    openAi.models = namedModels({
        QStringLiteral("gpt-5.6-luna"),
        QStringLiteral("gpt-5.6-terra"),
        QStringLiteral("gpt-5.6-sol"),
        QStringLiteral("gpt-5.5"),
        QStringLiteral("gpt-5.4-nano"),
        QStringLiteral("gpt-5.4-mini"),
        QStringLiteral("gpt-5.4"),
    });
    openAi.model = &RefinementSettings::openAiModel;
    openAi.effortRowId = QStringLiteral("openAiEffort");
    openAi.effortLabel = QStringLiteral("OpenAI effort");
    openAi.effortHelp = QStringLiteral("Reasoning effort used for refinement.");
    openAi.effortTooltip =
        QStringLiteral("OpenAI Responses reasoning.effort. Supported values vary by model.");
    openAi.efforts = {
        {QStringLiteral("none"), QStringLiteral("None")},
        {QStringLiteral("low"), QStringLiteral("Low")},
        {QStringLiteral("medium"), QStringLiteral("Medium")},
        {QStringLiteral("high"), QStringLiteral("High")},
        {QStringLiteral("xhigh"), QStringLiteral("Extra high")},
    };
    openAi.effort = &RefinementSettings::openAiEffort;
    openAi.fastModeRowId = QStringLiteral("openAiFastMode");
    openAi.fastModeHelp = QStringLiteral("1.5x speed and increased usage (negligible).");
    openAi.fastModeTooltip =
        QStringLiteral("Falls back to standard processing when a fast request fails.");
    openAi.fastMode = &RefinementSettings::openAiFastMode;
    openAi.authRows = {
        customRow(QStringLiteral("openAiAuthMode"),
                  QStringLiteral("OpenAI auth mode"),
                  QStringLiteral("Credential source for OpenAI refinement and ChatGPT Codex dictation.")),
        customRow(QStringLiteral("openAiCliproxyAccount"),
                  QStringLiteral("OpenAI CLI Proxy account"),
                  QStringLiteral("CLI Proxy API Codex account used for dictation and local refinement.")),
        customRow(QStringLiteral("openAiAuth"),
                  QStringLiteral("OpenAI auth"),
                  QStringLiteral("Current credential source, app settings key, or CLI Proxy "
                                 "API account.")),
    };
    openAi.authRows[0].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.openAiAuthMode);
    };
    openAi.authRows[0].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.openAiAuthMode = value.toString();
    };
    openAi.authRows[1].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.openAiCliproxyAccount);
    };
    openAi.authRows[1].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.openAiCliproxyAccount = value.toString();
    };
    openAi.authRows[1].visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.openAiAuthMode == kCliProxyAuthMode;
    };
    // Reading the app settings key means asking the keyring.
    openAi.authRows[2].expensive = true;

    ProviderAccount anthropic;
    anthropic.sectionTitle = QStringLiteral("Anthropic account");
    // The page's closing note, which belongs under the last card there is.
    anthropic.note = QStringLiteral(
        "Automatic OpenAI auth follows the Codex auth mode when available, then falls back to "
        "Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses "
        "the ChatGPT Codex backend. API-key modes apply to refinement; dictation uses Codex "
        "OAuth or the selected CLI Proxy API Codex account. The app settings key is stored in "
        "the desktop keyring through QtKeychain when available. CLI Proxy API auth reads OAuth "
        "accounts from its auto-detected auth directory. With a server URL configured, only "
        "refinement goes through that server; dictation still uses the selected local account.");
    anthropic.modelRowId = QStringLiteral("anthropicModel");
    anthropic.modelLabel = QStringLiteral("Claude model");
    anthropic.modelHelp = QStringLiteral("Model used for Anthropic refinement.");
    anthropic.modelTooltip =
        QStringLiteral("Defaults to Claude Sonnet 4.6. Select a model or type another model ID.");
    anthropic.modelWidthHint = 24;
    anthropic.models = {
        {QStringLiteral("claude-opus-4-8"), QStringLiteral("Claude Opus 4.8")},
        {QStringLiteral("claude-sonnet-4-6"), QStringLiteral("Claude Sonnet 4.6")},
        {QStringLiteral("claude-haiku-4-5-20251001"), QStringLiteral("Claude Haiku 4.5")},
    };
    anthropic.model = &RefinementSettings::anthropicModel;
    anthropic.cautionWhenModelContains = QStringLiteral("haiku");
    anthropic.caution = QStringLiteral("Haiku may treat transcript as instructions.");
    anthropic.effortRowId = QStringLiteral("anthropicEffort");
    anthropic.effortLabel = QStringLiteral("Claude effort");
    anthropic.effortHelp =
        QStringLiteral("Token spend and reasoning depth for Anthropic refinement.");
    anthropic.effortTooltip =
        QStringLiteral("Claude effort. Anthropic API support depends on the selected model.");
    anthropic.efforts = {
        {QStringLiteral("low"), QStringLiteral("Low")},
        {QStringLiteral("medium"), QStringLiteral("Medium")},
        {QStringLiteral("high"), QStringLiteral("High")},
        {QStringLiteral("xhigh"), QStringLiteral("Extra high")},
        {QStringLiteral("max"), QStringLiteral("Max")},
    };
    anthropic.effort = &RefinementSettings::anthropicEffort;
    anthropic.fastModeRowId = QStringLiteral("anthropicFastMode");
    anthropic.fastModeHelp = QStringLiteral("Faster refinement will use usage credits.");
    anthropic.fastModeTooltip =
        QStringLiteral("Only Opus models support fast mode; other models refine at standard speed.");
    anthropic.fastMode = &RefinementSettings::anthropicFastMode;
    anthropic.authRows = {
        customRow(QStringLiteral("anthropicAuthMode"),
                  QStringLiteral("Anthropic auth"),
                  QStringLiteral("Credential source for Anthropic refinement and Claude Voice dictation.")),
        customRow(QStringLiteral("anthropicCliproxyAccount"),
                  QStringLiteral("Claude CLI Proxy account"),
                  QStringLiteral("CLI Proxy API Claude account used for dictation and local refinement.")),
    };
    anthropic.authRows[0].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.anthropicAuthMode);
    };
    anthropic.authRows[0].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.anthropicAuthMode = value.toString();
    };
    anthropic.authRows[1].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.anthropicCliproxyAccount);
    };
    anthropic.authRows[1].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.anthropicCliproxyAccount = value.toString();
    };
    anthropic.authRows[1].visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.anthropicAuthMode == kCliProxyAuthMode;
    };

    return {openAi, anthropic};
}

// Only a provider actually routed through the CLI Proxy API server needs this
// card; a person using API keys or CLI tokens directly has nothing to set here.
bool cliproxyServerRowVisible(const AppSettings &settings, const Capabilities &)
{
    return settings.refinement.openAiAuthMode == kCliProxyAuthMode
        || settings.refinement.anthropicAuthMode == kCliProxyAuthMode;
}

SettingsSection cliproxyServerSection()
{
    SettingsRow baseUrl = customRow(
        QStringLiteral("cliproxyBaseUrl"),
        QStringLiteral("Server URL"),
        QStringLiteral("CLI Proxy API server to send refinement through. When set, the server "
                       "picks and refreshes accounts; leave empty to read its local token files."));
    baseUrl.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyBaseUrl);
    };
    baseUrl.apply = [](AppSettings &settings, const QVariant &value) {
        QString base = value.toString().trimmed();
        while (base.endsWith(QLatin1Char('/'))) {
            base.chop(1);
        }
        settings.refinement.cliproxyBaseUrl = base;
    };
    baseUrl.visible = cliproxyServerRowVisible;

    SettingsRow apiKey = customRow(
        QStringLiteral("cliproxyApiKey"),
        QStringLiteral("Server API key"),
        QStringLiteral("An entry from the server's api-keys list. Required when the server URL is set."));
    apiKey.tooltip = QStringLiteral("Stored unencrypted in Speecher's settings file.");
    apiKey.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyApiKey);
    };
    apiKey.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.cliproxyApiKey = value.toString().trimmed();
    };
    apiKey.visible = cliproxyServerRowVisible;

    return {QStringLiteral("CLI Proxy API"), QString(), {std::move(baseUrl), std::move(apiKey)}};
}

SettingsSection providerSection(const ProviderAccount &account)
{
    SettingsRow model;
    model.id = account.modelRowId;
    model.label = account.modelLabel;
    model.help = account.modelHelp;
    model.kind = RowKind::Text;
    model.tooltip = account.modelTooltip;
    model.contentWidthHint = account.modelWidthHint;
    model.suggestions = fixedOptions(account.models);
    model.value = [field = account.model](const AppSettings &settings) {
        return QVariant(settings.refinement.*field);
    };
    model.apply = [field = account.model](AppSettings &settings, const QVariant &value) {
        settings.refinement.*field = value.toString();
    };

    QList<SettingsRow> rows{std::move(model)};
    if (!account.caution.isEmpty()) {
        SettingsRow caution = infoRow(account.modelRowId + QStringLiteral("Caution"),
                                      QStringLiteral("Caution"),
                                      QString(),
                                      account.caution);
        caution.visible = [field = account.model,
                           needle = account.cautionWhenModelContains](const AppSettings &settings,
                                                                     const Capabilities &) {
            return (settings.refinement.*field).toCaseFolded().contains(needle);
        };
        rows.append(std::move(caution));
    }
    rows.append(choiceRow(
        account.effortRowId,
        account.effortLabel,
        account.effortHelp,
        fixedOptions(account.efforts),
        [field = account.effort](const AppSettings &settings) { return settings.refinement.*field; },
        [field = account.effort](AppSettings &settings, const QString &value) {
            settings.refinement.*field = value;
        }));
    rows.last().tooltip = account.effortTooltip;
    rows.append(toggleRow(
        account.fastModeRowId,
        QStringLiteral("Fast mode"),
        account.fastModeHelp,
        [field = account.fastMode](const AppSettings &settings) { return settings.refinement.*field; },
        [field = account.fastMode](AppSettings &settings, bool value) {
            settings.refinement.*field = value;
        }));
    rows.last().tooltip = account.fastModeTooltip;
    rows.append(account.authRows);
    return {account.sectionTitle, account.note, rows};
}

SettingsPage providersPage()
{
    QList<SettingsSection> sections;
    for (const ProviderAccount &account : providerAccounts()) {
        sections.append(providerSection(account));
    }
    sections.append(cliproxyServerSection());
    return {
        QStringLiteral("providers"),
        QStringLiteral("Providers"),
        QStringLiteral("preferences-system-network"),
        QStringLiteral("key.horizontal"),
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

int compareBaseVersions(const QString &left, const QString &right)
{
    static const QRegularExpression leadingDigits(QStringLiteral("^\\d+"));
    const auto component = [](const QStringList &parts, int index) {
        if (index >= parts.size()) {
            return 0;
        }
        return leadingDigits.match(parts.at(index)).captured().toInt();
    };
    const QStringList leftParts = left.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    const QStringList rightParts = right.section(QLatin1Char('-'), 0, 0).split(QLatin1Char('.'));
    for (int index = 0; index < qMax(leftParts.size(), rightParts.size()); ++index) {
        const int leftPart = component(leftParts, index);
        const int rightPart = component(rightParts, index);
        if (leftPart != rightPart) {
            return leftPart < rightPart ? -1 : 1;
        }
    }
    return 0;
}

SettingsSchema buildSettingsSchema(const SchemaContext &context)
{
    QList<SettingsPage> pages{generalPage(context),
                              audioPage(context),
                              applicationsPage(),
                              outputPage(context),
                              refinementPage(context),
                              vocabularyPage(),
                              correctionsPage(),
                              bindingsPage(),
                              providersPage()};
    pages.append(whatsNewPage(pages, context));
    return {std::move(pages)};
}

} // namespace speecher
