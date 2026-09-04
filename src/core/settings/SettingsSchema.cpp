#include "core/settings/SettingsSchema.h"

#include "core/CliProxyUrl.h"

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
                        QStringLiteral("Direct insertion (desktop accessibility)")});
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
    return QStringLiteral("Use the app's desktop ID, such as org.kde.konsole.");
#endif
}

QString applicationPasteRuleHint()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Overrides the paste behaviour for one bundle identifier, such as com.apple.Terminal");
#else
    return QStringLiteral("Overrides the paste behaviour for one application ID, such as org.kde.konsole");
#endif
}

// One sentence naming the platform's accessibility feature and what it unlocks.
// macOS calls it the Accessibility permission; Linux desktops expose AT-SPI,
// which the rest of the UI calls desktop accessibility.
QString accessibilityGateHelp(const QString &purpose)
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Grant Accessibility permission to %1").arg(purpose);
#else
    return QStringLiteral("Turn on desktop accessibility to %1").arg(purpose);
#endif
}

QString targetAccessibilityHint()
{
    return accessibilityGateHelp(QStringLiteral("identify the target application"));
}

// The action the front end runs to lift an accessibility gate, and the row
// state that names it. Every row that needs a known target declares this.
const QString kEnableAccessibilityAction = QStringLiteral("enableAccessibility");

void gateOnTargetAccessibility(SettingsRow &row, const QString &help)
{
    row.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.targetAccessibility;
    };
    row.disabledHelp = help;
    row.disabledAction = kEnableAccessibilityAction;
#ifdef Q_OS_MACOS
    row.disabledActionLabel = QStringLiteral("Open Accessibility settings");
#else
    row.disabledActionLabel = QStringLiteral("Enable desktop accessibility");
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
    if (selected.isEmpty()) {
        const auto note = std::find_if(notes.cbegin(), notes.cend(), [&context](const Note &candidate) {
            return compareBaseVersions(candidate.version, context.currentVersion) <= 0;
        });
        if (note != notes.cend()) {
            selected.append(*note);
        }
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
    QList<SettingsSection> sections;
#ifdef Q_OS_MACOS
    // Linux desktops decide the colour scheme themselves, so only macOS offers
    // the choice.
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
    // A desktop that ignores the request would otherwise offer Light and Dark
    // as if they did something.
    theme.enabled = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.colorSchemeOverride;
    };
    theme.disabledHelp = QStringLiteral(
        "This desktop chooses the colour scheme itself, so Speecher follows it");
    sections.append({QStringLiteral("Appearance"), QString(), {std::move(theme)}});
#endif

    sections.append({
        QStringLiteral("Dictation"),
        QString(),
        {
            toggleRow(QStringLiteral("pauseMedia"),
                      QStringLiteral("Pause media while dictating"),
                      QString(),
                      [](const AppSettings &settings) { return settings.ui.pauseMediaDuringTranscription; },
                      [](AppSettings &settings, bool value) { settings.ui.pauseMediaDuringTranscription = value; }),
            toggleRow(QStringLiteral("soundsEnabled"),
                      QStringLiteral("Play a sound when dictation starts and stops"),
                      QString(),
                      [](const AppSettings &settings) { return settings.ui.soundsEnabled; },
                      [](AppSettings &settings, bool value) { settings.ui.soundsEnabled = value; }),
            numberRow(QStringLiteral("previewWords"),
                      QStringLiteral("Live preview"),
                      QStringLiteral("Recent words the popup shows while you speak"),
                      {1, 40, 1, QString()},
                      [](const AppSettings &settings) { return settings.ui.previewWords; },
                      [](AppSettings &settings, int value) { settings.ui.previewWords = value; }),
        },
    });

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    sections.append({
        QStringLiteral("Startup"),
        QString(),
        {toggleRow(QStringLiteral("launchAtLogin"),
                   QStringLiteral("Start Speecher at login"),
                   QStringLiteral("Dictation only works while Speecher is running"),
                   [](const AppSettings &settings) { return settings.launchAtLogin; },
                   [](AppSettings &settings, bool value) { settings.launchAtLogin = value; })},
    });
#endif

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
    updateChannel.sinceVersion = QStringLiteral("0.1.0");
    SettingsRow autoCheck = toggleRow(
        QStringLiteral("autoCheckUpdates"),
        QStringLiteral("Check for updates automatically"),
        QString(),
        [](const AppSettings &settings) { return settings.updates.autoCheck; },
        [](AppSettings &settings, bool value) { settings.updates.autoCheck = value; });
    autoCheck.sinceVersion = QStringLiteral("0.1.0");
    autoCheck.tooltip = QStringLiteral("Checks the selected Update Channel at startup and once a day.");
    SettingsRow autoInstall = toggleRow(
        QStringLiteral("autoInstallUpdates"),
        QStringLiteral("Install updates automatically"),
#ifdef Q_OS_MACOS
        QStringLiteral("Downloads in the background and asks before installing"),
#else
        QStringLiteral("Installs in the background and takes effect after a restart"),
#endif
        [](const AppSettings &settings) { return settings.updates.autoInstall; },
        [](AppSettings &settings, bool value) { settings.updates.autoInstall = value; });
    autoInstall.sinceVersion = QStringLiteral("0.1.0");
    autoInstall.visible = [](const AppSettings &, const Capabilities &capabilities) {
        return capabilities.automaticUpdateDownloads;
    };
    // The version the person is running is the one line worth reading beside
    // the check; a front end with an updater replaces it with the check's
    // outcome while there is one.
    SettingsRow checkForUpdates = actionRow(
        QStringLiteral("checkForUpdates"),
        QStringLiteral("Check for updates"),
        context.currentVersion.isEmpty() ? QStringLiteral("Version unknown")
                                         : QStringLiteral("Speecher %1").arg(context.currentVersion),
        QStringLiteral("Check now"));
    sections.append({
        QStringLiteral("Updates"),
        QString(),
        {
            std::move(updateChannel),
            std::move(autoCheck),
            std::move(autoInstall),
            std::move(checkForUpdates),
            actionRow(kWhatsNewAction,
                      QStringLiteral("What's New"),
                      QStringLiteral("Release notes for this version"),
                      QStringLiteral("Open")),
        },
    });

    QList<SettingsRow> setupRows{
        actionRow(QStringLiteral("runSetup"),
                  QStringLiteral("First-run steps"),
                  QStringLiteral("Review the choices shown when Speecher first opened"),
                  QStringLiteral("Open assistant…")),
    };
#ifdef Q_OS_LINUX
    // Undoing the per-user install is the app's job on Linux: there is no
    // package manager entry for an AppImage to fall back on.
    setupRows.append(actionRow(
        QStringLiteral("removeSpeecher"),
        QStringLiteral("Remove Speecher from this computer"),
        QStringLiteral("Undoes startup, the app menu entry, the speecher command, the icon and the Global Shortcut"),
        QStringLiteral("Remove…")));
#endif
    sections.append({QStringLiteral("Setup"), QString(), std::move(setupRows)});

    return {
        QStringLiteral("general"),
        QStringLiteral("General"),
        QStringLiteral("preferences-system"),
        QStringLiteral("gearshape"),
        std::move(sections),
    };
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

    SettingsRow notes = customRow(kWhatsNewNotes, QString(), QString());
    const QString markdown = releaseNotesMarkdown(context);
    notes.value = [markdown](const AppSettings &) { return QVariant(markdown); };
    QList<SettingsSection> sections{
        {QStringLiteral("Release notes"), QString(), {std::move(notes)}}};
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
        QStringLiteral("Service"),
        QString(),
        fixedOptions(context.speechProviders),
        [](const AppSettings &settings) { return settings.speech.providerId; },
        [](AppSettings &settings, const QString &value) { settings.speech.providerId = value; });
    speechProvider.tooltip = QStringLiteral("The service that turns speech into text.");
    speechProvider.contentWidthHint = 24;

    SettingsRow device = choiceRow(
        QStringLiteral("audioDevice"),
        QStringLiteral("Device"),
        QString(),
        [lister = context.audioInputDevices](const AppSettings &settings) {
            return audioDeviceOptions(lister ? lister() : QList<RowOption>(), settings.audio.deviceId);
        },
        [](const AppSettings &settings) { return settings.audio.deviceId; },
        [](AppSettings &settings, const QString &value) { settings.audio.deviceId = value; });
    device.tooltip = QStringLiteral("The microphone Speecher records from.");
    device.contentWidthHint = 28;
    device.expensive = true;

    SettingsRow captureMode = choiceRow(
        QStringLiteral("captureMode"),
        QStringLiteral("Microphone use"),
        QStringLiteral("Keeping it open starts dictation faster but shows the microphone as in use"),
        fixedOptions({
            {QStringLiteral("on_demand"), QStringLiteral("Open only while dictating")},
            {QStringLiteral("warm"), QStringLiteral("Keep open between dictations")},
        }),
        [](const AppSettings &settings) { return settings.audio.mode; },
        [](AppSettings &settings, const QString &value) { settings.audio.mode = value; });

    SettingsRow vadEnabled = toggleRow(
        QStringLiteral("vadEnabled"),
        QStringLiteral("Skip silence"),
        QStringLiteral("Leaves out quiet stretches before, after and between sentences"),
        [](const AppSettings &settings) { return settings.audio.vadEnabled; },
        [](AppSettings &settings, bool value) { settings.audio.vadEnabled = value; });
    vadEnabled.tooltip = QStringLiteral(
        "Quiet stretches are left out of what is sent for transcription.");

    SettingsRow vadThreshold = numberRow(
        QStringLiteral("vadThresholdPercent"),
        QStringLiteral("Counts as quiet below"),
        QStringLiteral("Raise it if noise gets through, lower it if soft speech is cut"),
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
            {QStringLiteral("Microphone"), QString(), {std::move(device), std::move(captureMode)}},
            {QStringLiteral("Silence detection"),
             QString(),
             {std::move(vadEnabled), std::move(vadThreshold)}},
            // Timing controls most people never need; the labels say what a
            // change does to the recording rather than how the pipeline works.
            {QStringLiteral("Timing"),
             QString(),
             {
                 numberRow(QStringLiteral("preRollMs"),
                           QStringLiteral("Keep before speech"),
                           QStringLiteral("Prevents the first word from being clipped"),
                           {0, 1500, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.preRollMs; },
                           [](AppSettings &settings, int value) { settings.audio.preRollMs = value; }),
                 numberRow(QStringLiteral("postRollMs"),
                           QStringLiteral("Keep after stopping"),
                           QStringLiteral("Prevents the last word from being clipped"),
                           {0, 1500, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.postRollMs; },
                           [](AppSettings &settings, int value) { settings.audio.postRollMs = value; }),
                 numberRow(QStringLiteral("readinessTimeoutMs"),
                           QStringLiteral("Wait for microphone"),
                           QStringLiteral("How long to wait for sound before giving up"),
                           {500, 3000, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.audio.readinessTimeoutMs; },
                           [](AppSettings &settings, int value) { settings.audio.readinessTimeoutMs = value; }),
             }},
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

SettingsRow appRecognitionRuleRow()
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
    rules.minimumVisibleRows = 5;

    return collectionRow(
        QStringLiteral("appRecognitionRules"),
        QStringLiteral("Application recognition"),
        QStringLiteral("Custom matches beat the built-in ones and set the app type, the Writing Profile or both"),
        std::move(rules));
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
    descriptor.minimumVisibleRows = 3;

    return collectionRow(QStringLiteral("applicationPasteRules"),
                         QStringLiteral("App-specific paste rules"),
                         applicationPasteRuleHint(),
                         std::move(descriptor));
}

SettingsRow categoryPasteRuleRow(AppCategory category)
{
    const QString match = appCategoryName(category);
    return choiceRow(
        QStringLiteral("categoryPasteRule_") + match,
        pasteCategoryLabel(category),
        QString(),
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
                                   QString());
    method.tooltip = QStringLiteral("How Speecher delivers final text.");
    method.value = [](const AppSettings &settings) { return QVariant(settings.output.method); };
    method.apply = [](AppSettings &settings, const QVariant &value) {
        settings.output.method = value.toString();
    };
    QList<SettingsRow> insertionRows{std::move(method)};
    if (context.virtualKeyboardSetup) {
        insertionRows.append(customRow(QStringLiteral("virtualKeyboard"),
                                       QStringLiteral("Virtual keyboard"),
                                       QString()));
    }
    insertionRows.append(toggleRow(
        QStringLiteral("restoreClipboardAfterTyping"),
        QStringLiteral("Restore the clipboard"),
        restoreClipboardDescription(),
        [](const AppSettings &settings) { return settings.output.restoreClipboardAfterTyping; },
        [](AppSettings &settings, bool value) { settings.output.restoreClipboardAfterTyping = value; }));

    QList<SettingsRow> ruleRows{choiceRow(
        QStringLiteral("globalPasteRule"),
        QStringLiteral("Global fallback"),
        QStringLiteral("Used when no app rule matches"),
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
        ruleRows.append(categoryPasteRuleRow(category));
    }
    ruleRows.append(appRecognitionRuleRow());
    ruleRows.append(applicationPasteRuleRow());
    // Every row below the global fallback needs a known target application, so
    // they stand or fall together with desktop accessibility.
    for (int index = 1; index < ruleRows.size(); ++index) {
        ruleRows[index].groupId = QStringLiteral("targetPasteControls");
        gateOnTargetAccessibility(ruleRows[index], targetAccessibilityHint());
    }

    return {
        QStringLiteral("output"),
        QStringLiteral("Output"),
        QStringLiteral("klipper"),
        QStringLiteral("doc.on.clipboard"),
        {
            {QStringLiteral("How text is inserted"), QString(), std::move(insertionRows)},
            {QStringLiteral("Per-app rules"),
             QStringLiteral("Choose how recognised apps receive text"),
             std::move(ruleRows)},
            {QStringLiteral("Feedback"),
             QString(),
             {
                 numberRow(QStringLiteral("completionStatusDuration"),
                           QStringLiteral("Status duration"),
                           QStringLiteral("How long the delivery result stays visible"),
                           {0, 5000, 50, QStringLiteral(" ms")},
                           [](const AppSettings &settings) { return settings.output.completionStatusDurationMs; },
                           [](AppSettings &settings, int value) {
                               settings.output.completionStatusDurationMs = value;
                           }),
                 choiceRow(QStringLiteral("outputFormat"),
                           QStringLiteral("Format"),
                           QStringLiteral("What the clipboard holds unless a CLI shortcut asks otherwise"),
                           fixedOptions({
                               {QStringLiteral("plain"), QStringLiteral("Plain text")},
                               {QStringLiteral("html"), QStringLiteral("HTML and plain text")},
                           }),
                           [](const AppSettings &settings) { return outputFormatName(settings.output.format); },
                           [](AppSettings &settings, const QString &value) {
                               settings.output.format = outputFormatFromString(value);
                           }),
             }},
        },
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

SettingsRow vocabularyTermsRow()
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
    terms.minimumVisibleRows = 6;
    return collectionRow(QStringLiteral("vocabularyEntries"),
                         QStringLiteral("Terms"),
                         QString(),
                         std::move(terms));
}

SettingsRow vocabularyLimitRow()
{
    SettingsRow limit;
    limit.id = QStringLiteral("vocabularyLimit");
    limit.label = QStringLiteral("Limit");
    limit.help = QStringLiteral("Starred terms are sent first when the list is longer than the service accepts");
    limit.kind = RowKind::Info;
    limit.value = [](const AppSettings &settings) {
        return QVariant(VocabularyLimit::summary(
            vocabularyTerms(normalizeVocabularyEntries(settings.vocabulary))));
    };
    return limit;
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

SettingsRow correctionLearningRow()
{
    SettingsRow learn = toggleRow(
        QStringLiteral("correctionLearningControl"),
        QStringLiteral("Learn corrections"),
        QStringLiteral("Watch for edits after inserting text"),
        [](const AppSettings &settings) { return settings.correctionLearningEnabled; },
        [](AppSettings &settings, bool value) { settings.correctionLearningEnabled = value; });
    learn.tooltip = QStringLiteral("Observe a verified inserted span briefly and automatically "
                                   "learn high-confidence or repeated corrections.");
    gateOnTargetAccessibility(
        learn, accessibilityGateHelp(QStringLiteral("learn corrections after insertion")));
    return learn;
}

SettingsRow learnedCorrectionsRow()
{
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
    corrections.minimumVisibleRows = 6;
    return collectionRow(QStringLiteral("learnedCorrections"),
                         QStringLiteral("Corrections"),
                         QString(),
                         std::move(corrections));
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

SettingsRow bindingRulesRow()
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
    replacements.minimumVisibleRows = 5;
    return collectionRow(QStringLiteral("bindingRules"),
                         QStringLiteral("Replacements"),
                         QString(),
                         std::move(replacements));
}

SettingsPage vocabularyPage()
{
    return {
        QStringLiteral("vocabulary"),
        QStringLiteral("Vocabulary"),
        QStringLiteral("accessories-dictionary"),
        QStringLiteral("character.book.closed"),
        {
            {QStringLiteral("Vocabulary"),
             QStringLiteral("Names and words Speecher should recognise"),
             {vocabularyTermsRow(), vocabularyLimitRow()}},
            {QStringLiteral("Learned corrections"),
             QStringLiteral("Review fixes Speecher learned from your edits"),
             {correctionLearningRow(), learnedCorrectionsRow()}},
            {QStringLiteral("Replacements and snippets"),
             QStringLiteral("Spoken phrases replaced with exact text, even several lines of it"),
             {bindingRulesRow()}},
        },
    };
}

// What one refinement service contributes: a model card on the Refinement page
// and an account card on the Accounts page. A third service is another entry
// in providerAccounts() plus the AppSettings fields it names, rather than two
// more hand-written cards.
struct ProviderAccount {
    // The account card's title, in the words a person signs in with.
    QString accountTitle;
    // One line under the account card's title.
    QString accountNote;
    // The model card's title.
    QString modelTitle;
    QString modelRowId;
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
    openAi.accountTitle = QStringLiteral("ChatGPT / Codex");
    openAi.accountNote = QStringLiteral(
        "Automatic uses the first sign-in it finds: the Codex app, then an API key from the "
        "environment or saved in Speecher");
    openAi.modelTitle = QStringLiteral("OpenAI");
    openAi.modelRowId = QStringLiteral("openAiModel");
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
    openAi.effortLabel = QStringLiteral("Reasoning effort");
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
    openAi.fastModeHelp = QStringLiteral("Faster answers for slightly more usage");
    openAi.fastModeTooltip =
        QStringLiteral("Falls back to standard processing when a fast request fails.");
    openAi.fastMode = &RefinementSettings::openAiFastMode;
    // The status comes first: what the sign-in amounts to right now, then how
    // it is chosen.
    openAi.authRows = {
        customRow(QStringLiteral("openAiAuth"), QStringLiteral("Account"), QString()),
        customRow(QStringLiteral("openAiAuthMode"), QStringLiteral("Sign-in"), QString()),
        customRow(QStringLiteral("openAiCliproxyAccount"),
                  QStringLiteral("CLI Proxy API account"),
                  QString()),
    };
    // Reading the app settings key means asking the keyring.
    openAi.authRows[0].expensive = true;
    openAi.authRows[1].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.openAiAuthMode);
    };
    openAi.authRows[1].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.openAiAuthMode = value.toString();
    };
    openAi.authRows[2].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.openAiCliproxyAccount);
    };
    openAi.authRows[2].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.openAiCliproxyAccount = value.toString();
    };
    openAi.authRows[2].visible = [](const AppSettings &settings, const Capabilities &) {
        return settings.refinement.openAiAuthMode == kCliProxyAuthMode;
    };

    ProviderAccount anthropic;
    anthropic.accountTitle = QStringLiteral("Claude");
    anthropic.modelTitle = QStringLiteral("Claude");
    anthropic.modelRowId = QStringLiteral("anthropicModel");
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
    anthropic.effortLabel = QStringLiteral("Effort");
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
    anthropic.fastModeHelp = QStringLiteral("Faster refinement that uses usage credits");
    anthropic.fastModeTooltip =
        QStringLiteral("Only Opus models support fast mode; other models refine at standard speed.");
    anthropic.fastMode = &RefinementSettings::anthropicFastMode;
    anthropic.authRows = {
        customRow(QStringLiteral("anthropicAuth"), QStringLiteral("Account"), QString()),
        customRow(QStringLiteral("anthropicAuthMode"), QStringLiteral("Sign-in"), QString()),
        customRow(QStringLiteral("anthropicCliproxyAccount"),
                  QStringLiteral("CLI Proxy API account"),
                  QString()),
    };
    anthropic.authRows[1].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.anthropicAuthMode);
    };
    anthropic.authRows[1].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.anthropicAuthMode = value.toString();
    };
    anthropic.authRows[2].value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.anthropicCliproxyAccount);
    };
    anthropic.authRows[2].apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.anthropicCliproxyAccount = value.toString();
    };
    anthropic.authRows[2].visible = [](const AppSettings &settings, const Capabilities &) {
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
        QStringLiteral("Leave empty to use the account files on this computer"));
    baseUrl.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyBaseUrl);
    };
    baseUrl.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.cliproxyBaseUrl = cliproxyServerBase(value.toString());
    };
    baseUrl.visible = cliproxyServerRowVisible;

    SettingsRow apiKey = customRow(
        QStringLiteral("cliproxyApiKey"),
        QStringLiteral("API key"),
        QStringLiteral("One of the keys the server accepts, needed when a server URL is set"));
    apiKey.tooltip = QStringLiteral("Stored unencrypted in Speecher's settings file.");
    apiKey.value = [](const AppSettings &settings) {
        return QVariant(settings.refinement.cliproxyApiKey);
    };
    apiKey.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.cliproxyApiKey = value.toString().trimmed();
    };
    apiKey.visible = cliproxyServerRowVisible;

    return {QStringLiteral("CLI Proxy API server"),
            QStringLiteral("Routes text cleanup through the server instead of this computer's account files"),
            {std::move(baseUrl), std::move(apiKey)}};
}

SettingsSection modelSection(const ProviderAccount &account)
{
    SettingsRow model;
    model.id = account.modelRowId;
    model.label = QStringLiteral("Model");
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
        QString(),
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
    return {account.modelTitle, QString(), rows};
}

SettingsSection accountSection(const ProviderAccount &account)
{
    return {account.accountTitle, account.accountNote, account.authRows};
}

SettingsPage accountsPage()
{
    QList<SettingsSection> sections;
    for (const ProviderAccount &account : providerAccounts()) {
        sections.append(accountSection(account));
    }
    sections.append(cliproxyServerSection());
    return {
        QStringLiteral("accounts"),
        QStringLiteral("Accounts"),
        QStringLiteral("preferences-desktop-user-password"),
        QStringLiteral("person.badge.key"),
        sections,
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
        QStringLiteral("Send the target app's context"),
        QString(),
        [](const AppSettings &settings) { return settings.refinement.useTargetContext; },
        [](AppSettings &settings, bool value) { settings.refinement.useTargetContext = value; });
    gateOnTargetAccessibility(
        targetContext,
        accessibilityGateHelp(QStringLiteral("send the target app's context")));

    SettingsRow screenshots = toggleRow(
        QStringLiteral("includeScreenshotContext"),
        QStringLiteral("Allow screenshots as context"),
        QString(),
        [](const AppSettings &settings) { return settings.refinement.includeScreenshotContext; },
        [](AppSettings &settings, bool value) { settings.refinement.includeScreenshotContext = value; });
    screenshots.tooltip = QStringLiteral(
        "Captured through the desktop portal and kept only for the current dictation.");
    screenshots.disabledHelp = QStringLiteral(
        "Choose an image-capable OpenAI or Anthropic refiner to send screenshots");
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
    profileBehavior.kind = RowKind::Custom;
    profileBehavior.value = [](const AppSettings &settings) {
        return QVariant::fromValue(settings.refinement.writingProfiles);
    };
    profileBehavior.apply = [](AppSettings &settings, const QVariant &value) {
        settings.refinement.writingProfiles = value.value<QList<WritingProfileSettings>>();
    };

    QList<SettingsSection> sections{
        {QStringLiteral("Refinement"),
         QString(),
         {
             choiceRow(QStringLiteral("refinementProvider"),
                       QStringLiteral("Provider"),
                       QStringLiteral("None leaves the text as spoken"),
                       fixedOptions(refiners),
                       [](const AppSettings &settings) { return settings.refinement.providerId; },
                       [](AppSettings &settings, const QString &value) { settings.refinement.providerId = value; }),
             choiceRow(QStringLiteral("defaultWritingProfile"),
                       QStringLiteral("Fallback profile"),
                       QStringLiteral("Used when the target app does not imply one"),
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
        {QStringLiteral("Writing profiles"),
         QStringLiteral("Cleanup strength and an optional tone for each detected profile"),
         {std::move(profileBehavior)}},
    };
    for (const ProviderAccount &account : providerAccounts()) {
        sections.append(modelSection(account));
    }

    return {
        QStringLiteral("refinement"),
        QStringLiteral("Refinement"),
        QStringLiteral("tools-wizard"),
        QStringLiteral("wand.and.sparkles"),
        std::move(sections),
    };
}

} // namespace

QString restoreClipboardDescription()
{
    return QStringLiteral("Restore the previous clipboard after Speecher confirms the paste, or "
                          "after a short delay when it cannot");
}

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
    // The sidebar's order, which both front ends follow.
    QList<SettingsPage> pages{generalPage(context),
                              audioPage(context),
                              outputPage(context),
                              accountsPage(),
                              refinementPage(context),
                              vocabularyPage()};
    pages.append(whatsNewPage(pages, context));
    return {std::move(pages)};
}

} // namespace speecher
