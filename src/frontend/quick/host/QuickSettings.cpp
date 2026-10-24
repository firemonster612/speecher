#include "frontend/quick/host/QuickSettings.h"

#include "app/ApplicationController.h"
#include "core/EndpointSettings.h"
#include "core/EndpointUrl.h"
#include "core/ProviderChain.h"
#include "core/SettingsStore.h"

#include <QDebug>

#include <cstddef>
#include <type_traits>

namespace speecher {

namespace {

QuickSettings *g_instance = nullptr;

// What an update check frequency in this unit counts in minutes; zero for a
// unit the custom interval does not offer, such as a hand-edited "weeks".
int minutesPerCheckIntervalUnit(const QString &unit)
{
    if (unit == QLatin1String("days")) {
        return 24 * 60;
    }
    if (unit == QLatin1String("hours")) {
        return 60;
    }
    return unit == QLatin1String("minutes") ? 1 : 0;
}

// The unit a custom interval reads back in: the one it was given in, or else
// the largest that divides it evenly, so a day becomes 1 day, not 1440 minutes.
QString checkIntervalUnit(const UpdateSettings &updates)
{
    const auto divides = [&](const QString &unit) {
        const int minutes = minutesPerCheckIntervalUnit(unit);
        return minutes > 0 && updates.checkIntervalMinutes % minutes == 0;
    };
    if (divides(updates.checkIntervalUnit)) {
        return updates.checkIntervalUnit;
    }
    for (const QString unit : {QStringLiteral("days"), QStringLiteral("hours")}) {
        if (divides(unit)) {
            return unit;
        }
    }
    return QStringLiteral("minutes");
}

// One schema row the UI saves, as reads and writes of the settings. Values
// are what QML holds: strings, numbers, booleans, lists and plain objects,
// with choice values being the option ids the schema lists.
struct SettingRow {
    const char *id;
    QVariant (*read)(const AppSettings &);
    void (*write)(AppSettings &, const QVariant &);
};

// A row that is one AppSettings field, stored as QML hands it over.
#define FIELD_ROW(rowId, field)                                                     \
    {rowId, [](const AppSettings &s) { return QVariant::fromValue(s.field); },      \
     [](AppSettings &s, const QVariant &v) { s.field = v.value<std::remove_cvref_t<decltype(s.field)>>(); }}

// Every row the UI saves, by the schema's row id. Rows missing here (secrets,
// paste rules, the result duration, launch at login, acceleration and the
// graphics card) wait for the PRs that wire their pages. So do the rows whose
// choices come from a placeholder area, since a made-up choice would be
// saved as real: the microphone (Audio), the local model (Models), the local
// runner (Library's runners), the CLI Proxy accounts (Accounts) and the
// default Writing Profile (Library's profiles).
const SettingRow settingRows[] = {
    {"shortcutBehavior",
     [](const AppSettings &s) { return QVariant(shortcutActivationModeName(s.shortcutActivationMode)); },
     [](AppSettings &s, const QVariant &v) { s.shortcutActivationMode = shortcutActivationModeFromName(v.toString()); }},

    FIELD_ROW("captureMode", audio.mode),
    FIELD_ROW("readinessTimeoutMs", audio.readinessTimeoutMs),
    FIELD_ROW("preRollMs", audio.preRollMs),
    FIELD_ROW("postRollMs", audio.postRollMs),
    FIELD_ROW("vadEnabled", audio.vadEnabled),
    FIELD_ROW("vadThresholdPercent", audio.vadThresholdPercent),

    // A new primary keeps the fallbacks that are not it.
    {"speechProvider", [](const AppSettings &s) { return QVariant(s.speech.providerId); },
     [](AppSettings &s, const QVariant &v) {
         s.speech.providerId = v.toString();
         s.speech.fallbackProviderIds =
             normalizedFallbackProviders(ProviderRole::Speech, s.speech.providerId, s.speech.fallbackProviderIds);
     }},
    {"speechFallbackList", [](const AppSettings &s) { return QVariant(s.speech.fallbackProviderIds); },
     [](AppSettings &s, const QVariant &v) {
         s.speech.fallbackProviderIds =
             normalizedFallbackProviders(ProviderRole::Speech, s.speech.providerId, v.toStringList());
     }},
    FIELD_ROW("spokenLanguage", speech.language),
    FIELD_ROW("codexFinalRetranscribe", speech.codexFinalRetranscribe),
    FIELD_ROW("speechEndpointUrl", speech.endpoint.baseUrl),
    FIELD_ROW("speechEndpointPath", speech.endpoint.path),
    FIELD_ROW("speechEndpointModel", speech.endpoint.model),

    FIELD_ROW("showLiveText", ui.transcriptionPreviewEnabled),
    FIELD_ROW("showRefinementText", ui.refinementPreviewEnabled),
    FIELD_ROW("previewWords", ui.previewWords),
    FIELD_ROW("reviewSelectionEdits", ui.selectionEditReviewEnabled),
    FIELD_ROW("playSounds", ui.soundsEnabled),
    FIELD_ROW("pauseMedia", ui.pauseMediaDuringTranscription),

    // None clears the fallbacks.
    {"refinementProvider", [](const AppSettings &s) { return QVariant(s.refinement.providerId); },
     [](AppSettings &s, const QVariant &v) {
         s.refinement.providerId = v.toString();
         s.refinement.fallbackProviderIds = normalizedFallbackProviders(
             ProviderRole::Refinement, s.refinement.providerId, s.refinement.fallbackProviderIds);
     }},
    {"refinementFallbacks", [](const AppSettings &s) { return QVariant(s.refinement.fallbackProviderIds); },
     [](AppSettings &s, const QVariant &v) {
         s.refinement.fallbackProviderIds =
             normalizedFallbackProviders(ProviderRole::Refinement, s.refinement.providerId, v.toStringList());
     }},
    FIELD_ROW("openAiModel", refinement.openAiModel),
    FIELD_ROW("openAiEffort", refinement.openAiEffort),
    FIELD_ROW("openAiSpeed", refinement.openAiSpeed),
    FIELD_ROW("anthropicModel", refinement.anthropicModel),
    FIELD_ROW("anthropicEffort", refinement.anthropicEffort),
    {"anthropicFastMode",
     [](const AppSettings &s) {
         return QVariant(s.refinement.anthropicFastMode ? QStringLiteral("fast") : QStringLiteral("standard"));
     },
     [](AppSettings &s, const QVariant &v) { s.refinement.anthropicFastMode = v.toString() == QLatin1String("fast"); }},
    FIELD_ROW("localRunnerModel", refinement.localRunner.model),
    // Core stores the custom server as no preset.
    {"refinementEndpointServer",
     [](const AppSettings &s) {
         const QString preset = s.refinement.endpoint.preset;
         return QVariant(preset.isEmpty() ? QStringLiteral("custom") : preset);
     },
     [](AppSettings &s, const QVariant &v) {
         const QString server = v.toString();
         editRefinementEndpoint(s, {.preset = server == QLatin1String("custom") ? QString() : server});
     }},
    {"refinementEndpointFormat",
     [](const AppSettings &s) { return QVariant(resolvedRefinementEndpoint(s.refinement).format); },
     [](AppSettings &s, const QVariant &v) { editRefinementEndpoint(s, {.format = v.toString()}); }},
    {"refinementEndpointUrl",
     [](const AppSettings &s) { return QVariant(resolvedRefinementEndpoint(s.refinement).apiBase); },
     [](AppSettings &s, const QVariant &v) { editRefinementEndpoint(s, {.baseUrl = v.toString()}); }},
    {"refinementEndpointModel",
     [](const AppSettings &s) { return QVariant(resolvedRefinementEndpoint(s.refinement).model); },
     [](AppSettings &s, const QVariant &v) { editRefinementEndpoint(s, {.model = v.toString()}); }},
    FIELD_ROW("targetContextControl", refinement.useTargetContext),
    FIELD_ROW("includeScreenshotContext", refinement.includeScreenshotContext),
    FIELD_ROW("customSystemPromptEnabled", refinement.customSystemPromptEnabled),
    FIELD_ROW("customSystemPrompt", refinement.customSystemPrompt),

    FIELD_ROW("correctionLearningControl", correctionLearningEnabled),

    FIELD_ROW("outputMethod", output.method),
    {"outputFormat", [](const AppSettings &s) { return QVariant(outputFormatName(s.output.format)); },
     [](AppSettings &s, const QVariant &v) { s.output.format = outputFormatFromString(v.toString()); }},
    FIELD_ROW("restoreClipboardAfterTyping", output.restoreClipboardAfterTyping),

    FIELD_ROW("openAiAuthMode", refinement.openAiAuthMode),
    FIELD_ROW("anthropicAuthMode", refinement.anthropicAuthMode),
    // What was typed, not the directory detected from it.
    {"cliproxyOauthDir", [](const AppSettings &s) { return QVariant(s.refinement.cliproxyOauthDirConfigured); },
     [](AppSettings &s, const QVariant &v) { s.refinement.cliproxyOauthDirConfigured = v.toString().trimmed(); }},
    {"cliproxyBaseUrl", [](const AppSettings &s) { return QVariant(s.refinement.cliproxyBaseUrl); },
     [](AppSettings &s, const QVariant &v) { s.refinement.cliproxyBaseUrl = endpointServerBase(v.toString()); }},

    {"localIdleUnload", [](const AppSettings &s) { return QVariant(QString::number(s.speech.local.idleUnloadMinutes)); },
     [](AppSettings &s, const QVariant &v) { s.speech.local.idleUnloadMinutes = v.toString().toInt(); }},

    FIELD_ROW("themeControl", ui.theme),
    FIELD_ROW("insightsEnabled", insightsEnabled),

    {"updateChannel", [](const AppSettings &s) { return QVariant(updateChannelName(s.updates.channel)); },
     [](AppSettings &s, const QVariant &v) { s.updates.channel = updateChannelFromName(v.toString()); }},
    FIELD_ROW("autoCheckUpdates", updates.autoCheck),
    FIELD_ROW("autoInstallUpdates", updates.autoInstall),
    // A listed frequency is its minutes; an interval given in a unit is
    // "custom", and choosing Custom keeps the interval for the custom row.
    {"updateCheckInterval",
     [](const AppSettings &s) {
         return QVariant(s.updates.checkIntervalUnit.isEmpty() ? QString::number(s.updates.checkIntervalMinutes)
                                                               : QStringLiteral("custom"));
     },
     [](AppSettings &s, const QVariant &v) {
         if (v.toString() == QLatin1String("custom")) {
             s.updates.checkIntervalUnit = checkIntervalUnit(s.updates);
             return;
         }
         s.updates.checkIntervalUnit.clear();
         s.updates.checkIntervalMinutes = v.toString().toInt();
     }},
    {"updateCheckCustomInterval",
     [](const AppSettings &s) {
         const QString unit = checkIntervalUnit(s.updates);
         return QVariant(QVariantMap{
             {QStringLiteral("number"), s.updates.checkIntervalMinutes / minutesPerCheckIntervalUnit(unit)},
             {QStringLiteral("unit"), unit},
         });
     },
     [](AppSettings &s, const QVariant &v) {
         const QVariantMap interval = v.toMap();
         const QString unit = interval.value(QStringLiteral("unit")).toString();
         const int perUnit = minutesPerCheckIntervalUnit(unit);
         if (perUnit == 0) {
             qWarning().noquote() << "Settings.set(updateCheckCustomInterval) got an unknown unit:" << unit;
             return;
         }
         s.updates.checkIntervalUnit = unit;
         s.updates.checkIntervalMinutes = qBound(UpdateSettings::minimumCheckIntervalMinutes,
                                                 interval.value(QStringLiteral("number")).toInt() * perUnit,
                                                 UpdateSettings::maximumCheckIntervalMinutes);
     }},
};

#undef FIELD_ROW

// The Global Shortcuts, which the controller binds and keeps.
const struct ShortcutRow {
    const char *id;
    GlobalShortcutRole role;
} shortcutRows[] = {
    {"globalShortcut", GlobalShortcutRole::Dictation},
    {"cancelShortcut", GlobalShortcutRole::Cancel},
    {"pauseShortcut", GlobalShortcutRole::Pause},
};

const QString runSetupAction = QStringLiteral("runSetup");

// The id of the shortcut row for role.
QString shortcutRowId(GlobalShortcutRole role)
{
    for (const ShortcutRow &row : shortcutRows) {
        if (row.role == role) {
            return QLatin1String(row.id);
        }
    }
    return {};
}

// The row of either table with this id, or nullptr.
template <typename Row, std::size_t count>
const Row *findRow(const Row (&rows)[count], const QString &id)
{
    for (const Row &row : rows) {
        if (id == QLatin1String(row.id)) {
            return &row;
        }
    }
    return nullptr;
}

} // namespace

QStringList shortcutKeys(const QString &display)
{
    if (display.isEmpty()) {
        return {};
    }
    QStringList keys;
    qsizetype start = 0;
    for (qsizetype i = 0; i + 1 < display.size(); ++i) {
        if (display.at(i) == QLatin1Char('+') && i > start) {
            keys.append(display.mid(start, i - start));
            start = i + 1;
        }
    }
    keys.append(display.mid(start));
    return keys;
}

QuickSettings::QuickSettings(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    g_instance = this;
    SettingsStore *store = controller->settings();
    connect(store, &SettingsStore::snapshotApplied, this, &QuickSettings::refresh);
    connect(store, &SettingsStore::audioCaptureSettingsChanged, this, &QuickSettings::refresh);
    connect(store, &SettingsStore::correctionLearningEnabledChanged, this, &QuickSettings::refresh);
    connect(store, &SettingsStore::updateSettingsChanged, this, &QuickSettings::refresh);
    connect(controller, &ApplicationController::globalShortcutChanged, this, &QuickSettings::refresh);
    // The desktop's chooser answers later, and may refuse.
    connect(controller,
            &ApplicationController::globalShortcutRegistrationFinished,
            this,
            [this](bool bound, const QString &detail, GlobalShortcutRole role) {
                setError(shortcutRowId(role), bound ? QString() : detail);
            });
    refresh();
}

QuickSettings::~QuickSettings()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickSettings *QuickSettings::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickSettings::create", "the host makes Settings before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QVariantMap QuickSettings::values() const
{
    return m_values;
}

QVariantMap QuickSettings::errors() const
{
    return m_errors;
}

void QuickSettings::set(const QString &id, const QVariant &value)
{
    if (const ShortcutRow *shortcut = findRow(shortcutRows, id)) {
        setShortcut(id, shortcut->role, value.toStringList());
        return;
    }
    const SettingRow *row = findRow(settingRows, id);
    if (!row) {
        qWarning().noquote() << "Settings.set:" << id << "is not saved from the UI yet";
        return;
    }
    // The snapshot leaves unread keyring secrets unread, and applySnapshot
    // keeps them, so saving one row never clears a key.
    SettingsStore *store = m_controller->settings();
    AppSettings draft = store->dictationSnapshot();
    row->write(draft, value);
    store->applySnapshot(draft);
}

void QuickSettings::act(const QString &id)
{
    if (id == runSetupAction) {
        m_controller->showSetup();
        return;
    }
    if (desktopPicksKeys(id)) {
        setError(id, {});
        m_controller->registerGlobalShortcut(findRow(shortcutRows, id)->role);
        return;
    }
    qWarning().noquote() << "Settings.act:" << id << "is not an action the UI runs yet";
}

bool QuickSettings::saves(const QString &id) const
{
    return findRow(settingRows, id) || findRow(shortcutRows, id) || id == runSetupAction;
}

bool QuickSettings::desktopPicksKeys(const QString &id) const
{
    const ShortcutRow *shortcut = findRow(shortcutRows, id);
    return shortcut && m_controller->globalShortcutUsesDesktopChooser(shortcut->role);
}

void QuickSettings::startRecordingShortcut(const QString &id)
{
    // A refusal from the last attempt would read as a verdict on this one.
    setError(id, {});
    const bool suspended = !m_recordingShortcut.isEmpty();
    m_recordingShortcut = id;
    if (!suspended) {
        m_controller->suspendGlobalShortcut();
    }
}

void QuickSettings::stopRecordingShortcut(const QString &id)
{
    if (id != m_recordingShortcut) {
        return;
    }
    m_recordingShortcut.clear();
    GlobalShortcutRole failedRole = GlobalShortcutRole::Dictation;
    const QString error = m_controller->resumeGlobalShortcut(&failedRole);
    if (!error.isEmpty()) {
        setError(shortcutRowId(failedRole), error);
    }
}

void QuickSettings::setShortcut(const QString &id, GlobalShortcutRole role, const QStringList &keys)
{
    const QString text = keys.join(QLatin1Char('+'));
    const ShortcutBinding binding = ShortcutBinding::fromString(text);
    if (!keys.isEmpty() && binding.isEmpty()) {
        qWarning().noquote() << "Settings.set: the keys" << text << "are not a shortcut";
        return;
    }
    QString error;
    const bool bound = m_controller->setGlobalShortcut(binding, &error, role);
    setError(id, bound ? QString() : error);
}

void QuickSettings::setError(const QString &id, const QString &error)
{
    if (m_errors.value(id).toString() == error) {
        return;
    }
    if (error.isEmpty()) {
        m_errors.remove(id);
    } else {
        m_errors.insert(id, error);
    }
    emit errorsChanged();
}

void QuickSettings::refresh()
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    QVariantMap values;
    for (const SettingRow &row : settingRows) {
        values.insert(QLatin1String(row.id), row.read(settings));
    }
    for (const auto &row : shortcutRows) {
        const QString id = QLatin1String(row.id);
        // The display, not the binding: where the desktop picks the keys (the
        // portal), the binding is a stand-in and only the display is real.
        const QStringList keys = shortcutKeys(m_controller->globalShortcutDisplay(row.role));
        // A refusal was about the keys the row showed; keys changed from
        // anywhere else, such as the CLI, leave it stale.
        if (keys != m_values.value(id).toStringList()) {
            setError(id, {});
        }
        values.insert(id, keys);
    }
    if (values == m_values) {
        return;
    }
    m_values = values;
    emit valuesChanged();
}

} // namespace speecher
