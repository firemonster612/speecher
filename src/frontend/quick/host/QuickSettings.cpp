#include "frontend/quick/host/QuickSettings.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "core/EndpointSettings.h"
#include "core/EndpointUrl.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QmlValues.h"

#include <QCoreApplication>
#include <QDebug>
#include <QKeyEvent>

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>

namespace speecher {

namespace {

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

// The update check frequencies Check frequency lists, in minutes: its
// options in schema/updates.js.
constexpr int listedCheckIntervals[] = {5, 15, 30, 60, 360, 720, 1440, 10080};

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

// A Service Order as QML holds it: [{service, model, language, effort,
// speed}], the first service first.
QVariant serviceOrderValue(const ServiceOrder &services)
{
    QVariantList entries;
    for (const QString &id : services.order) {
        const ServiceChoice choice = services.choice(id);
        entries.append(QVariantMap{{QStringLiteral("service"), id},
                                   {QStringLiteral("model"), choice.model},
                                   {QStringLiteral("language"), choice.language},
                                   {QStringLiteral("effort"), choice.effort},
                                   {QStringLiteral("speed"), choice.speed}});
    }
    return entries;
}

// The order QML hands back. A field an entry leaves out keeps the stored
// choice's, and a service left out of the order keeps its choice for when
// it comes back.
// An order as v sets it, each entry keeping only the fields that differ from
// the same service's in shown: an order hands back every field as it was
// read, and replaying those would undo a model saved since by other means.
QVariant changedFields(const QVariant &shown, const QVariant &v)
{
    QVariantList entries;
    const QVariantList before = shown.toList();
    for (const QVariant &value : v.toList()) {
        QVariantMap entry = value.toMap();
        const auto same = std::find_if(before.cbegin(), before.cend(), [&entry](const QVariant &old) {
            return old.toMap().value(QStringLiteral("service")) == entry.value(QStringLiteral("service"));
        });
        if (same != before.cend()) {
            const QVariantMap old = same->toMap();
            for (auto it = entry.begin(); it != entry.end();) {
                it = it.key() != u"service" && old.value(it.key()) == it.value() ? entry.erase(it) : std::next(it);
            }
        }
        entries.append(entry);
    }
    return entries;
}

void setServiceOrder(ServiceOrder &services, const QVariant &v)
{
    services.order.clear();
    for (const QVariant &value : v.toList()) {
        const QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("service")).toString();
        services.order.append(id);
        ServiceChoice &choice = services.choice(id);
        for (const auto &[name, field] : {std::pair{"model", &ServiceChoice::model},
                                          std::pair{"language", &ServiceChoice::language},
                                          std::pair{"effort", &ServiceChoice::effort},
                                          std::pair{"speed", &ServiceChoice::speed}}) {
            if (entry.contains(QLatin1String(name))) {
                choice.*field = entry.value(QLatin1String(name)).toString();
            }
        }
    }
}

// A delivery step's row: its switch, which can't turn off the last step on.
#define DELIVERY_STEP_ROW(rowId, step)                                                       \
    {rowId, [](const AppSettings &s) { return QVariant(s.output.steps.has(step)); },        \
     [](AppSettings &s, const QVariant &v) { s.output.steps = s.output.steps.withStep(step, v.toBool()); }}

// One schema row the UI saves, as reads and writes of the settings. Values
// are what QML holds: strings, numbers, booleans, lists and plain objects,
// with choice values being the option ids the schema lists. refuse, where a
// row has one, says why a value can't be saved (core's words), or nothing.
struct SettingRow {
    const char *id;
    QVariant (*read)(const AppSettings &);
    void (*write)(AppSettings &, const QVariant &);
    QString (*refuse)(const QVariant &) = nullptr;
    // In place of write, for a row whose saved value depends on this
    // computer, such as which graphics cards it has.
    void (*writeHere)(AppSettings &, const QVariant &, const LiveFacts &) = nullptr;
};

// A row that is one AppSettings field, stored as QML hands it over.
#define FIELD_ROW(rowId, field)                                                     \
    {rowId, [](const AppSettings &s) { return QVariant::fromValue(s.field); },      \
     [](AppSettings &s, const QVariant &v) { s.field = v.value<std::remove_cvref_t<decltype(s.field)>>(); }}

// Every row the UI saves, by the schema's row id. Secrets are Accounts' to
// save.
const SettingRow settingRows[] = {
    {"shortcutBehavior",
     [](const AppSettings &s) { return QVariant(shortcutActivationModeName(s.shortcutActivationMode)); },
     [](AppSettings &s, const QVariant &v) { s.shortcutActivationMode = shortcutActivationModeFromName(v.toString()); }},

    FIELD_ROW("audioDevice", audio.deviceId),
    FIELD_ROW("captureMode", audio.mode),
    FIELD_ROW("readinessTimeoutMs", audio.readinessTimeoutMs),
    FIELD_ROW("preRollMs", audio.preRollMs),
    FIELD_ROW("postRollMs", audio.postRollMs),
    FIELD_ROW("vadEnabled", audio.vadEnabled),
    FIELD_ROW("vadThresholdPercent", audio.vadThresholdPercent),

    // [{service, model, language}], the first service first.
    {"speechServices", [](const AppSettings &s) { return serviceOrderValue(s.speech.services); },
     [](AppSettings &s, const QVariant &v) { setServiceOrder(s.speech.services, v); }},
    FIELD_ROW("spokenLanguage", speech.language),
    FIELD_ROW("codexFinalRetranscribe", speech.codexFinalRetranscribe),
    // The "local" entry's model, in the order or out of it. Choosing one
    // marks it chosen, so the hardware default stops replacing it.
    {"speechLocalModel", [](const AppSettings &s) { return QVariant(localModelId(s.speech)); },
     [](AppSettings &s, const QVariant &v) {
         localModelId(s.speech) = v.toString();
         s.speech.local.modelChosen = true;
     }},
    // The speech server among Your servers; its model is the "endpoint"
    // entry's in the order.
    FIELD_ROW("speechEndpointUrl", speech.server.baseUrl),
    FIELD_ROW("speechEndpointPath", speech.server.path),

    FIELD_ROW("showLiveText", ui.transcriptionPreviewEnabled),
    FIELD_ROW("showRefinementText", ui.refinementPreviewEnabled),
    FIELD_ROW("previewWords", ui.previewWords),
    FIELD_ROW("resultDuration", output.completionStatusDurationMs),
    FIELD_ROW("reviewSelectionEdits", ui.selectionEditReviewEnabled),
    FIELD_ROW("playSounds", ui.soundsEnabled),
    FIELD_ROW("pauseMedia", ui.pauseMediaDuringTranscription),

    // Clean up my words.
    FIELD_ROW("refinementEnabled", refinement.enabled),
    // [{service, model, effort, speed}], the first service first.
    {"refinementServices", [](const AppSettings &s) { return serviceOrderValue(s.refinement.services); },
     [](AppSettings &s, const QVariant &v) { setServiceOrder(s.refinement.services, v); }},
    // This computer's Local Runner; its cleanup model is the "local" entry's
    // in the order.
    FIELD_ROW("localRunner", refinement.localRunner.runner),
    // The refinement server among Your servers; its model is the "endpoint"
    // entry's in the order.
    {"refinementEndpointFormat", [](const AppSettings &s) { return QVariant(s.refinement.server.format); },
     [](AppSettings &s, const QVariant &v) { editRefinementEndpoint(s, {.format = v.toString()}); }},
    {"refinementEndpointUrl", [](const AppSettings &s) { return QVariant(s.refinement.server.baseUrl); },
     [](AppSettings &s, const QVariant &v) { editRefinementEndpoint(s, {.baseUrl = v.toString()}); }},
    FIELD_ROW("targetContextControl", refinement.useTargetContext),
    FIELD_ROW("includeScreenshotContext", refinement.includeScreenshotContext),
    FIELD_ROW("customSystemPromptEnabled", refinement.customSystemPromptEnabled),
    FIELD_ROW("customSystemPrompt", refinement.customSystemPrompt),
    FIELD_ROW("additionalInstructions", refinement.additionalInstructions),

    FIELD_ROW("correctionLearningControl", correctionLearningEnabled),

    DELIVERY_STEP_ROW("insertThroughAccessibility", DeliveryStep::Insert),
    DELIVERY_STEP_ROW("pressPasteKeys", DeliveryStep::Press),
    DELIVERY_STEP_ROW("keepOnClipboard", DeliveryStep::Keep),
    {"outputFormat", [](const AppSettings &s) { return QVariant(outputFormatName(s.output.format)); },
     [](AppSettings &s, const QVariant &v) {
         s.output.format = outputFormatFromString(v.toString()).value_or(OutputFormat::PlainText);
     }},

    {"openAiAuthMode", [](const AppSettings &s) { return QVariant(openAiAuthModeName(s.accounts.openAiAuthMode)); },
     [](AppSettings &s, const QVariant &v) { s.accounts.openAiAuthMode = openAiAuthModeFromName(v.toString()); }},
    {"anthropicAuthMode",
     [](const AppSettings &s) { return QVariant(anthropicAuthModeName(s.accounts.anthropicAuthMode)); },
     [](AppSettings &s, const QVariant &v) { s.accounts.anthropicAuthMode = anthropicAuthModeFromName(v.toString()); }},
    // A file name from Accounts.cliproxyAccounts.
    FIELD_ROW("openAiCliproxyAccount", accounts.openAiCliproxyAccount),
    FIELD_ROW("anthropicCliproxyAccount", accounts.anthropicCliproxyAccount),
    // What was typed, not the directory detected from it.
    {"cliproxyOauthDir", [](const AppSettings &s) { return QVariant(s.accounts.cliproxyOauthDirConfigured); },
     [](AppSettings &s, const QVariant &v) { s.accounts.cliproxyOauthDirConfigured = v.toString().trimmed(); }},
    {"cliproxyBaseUrl", [](const AppSettings &s) { return QVariant(s.accounts.cliproxyBaseUrl); },
     [](AppSettings &s, const QVariant &v) { s.accounts.cliproxyBaseUrl = endpointServerBase(v.toString()); }},

    {"localIdleUnload", [](const AppSettings &s) { return QVariant(QString::number(s.speech.local.idleUnloadMinutes)); },
     [](AppSettings &s, const QVariant &v) { s.speech.local.idleUnloadMinutes = v.toString().toInt(); }},
    {.id = "localAcceleration",
     .read = [](const AppSettings &s) { return QVariant(s.speech.local.runsOn.backend); },
     .writeHere = [](AppSettings &s, const QVariant &v, const LiveFacts &facts) {
         s.speech.local.runsOn = localRunsOnFor(facts.localGpus, v.toString());
     }},
    FIELD_ROW("localGraphicsCard", speech.local.runsOn.deviceId),

    FIELD_ROW("themeControl", ui.theme),
    FIELD_ROW("colorScheme", ui.colorScheme),
    FIELD_ROW("translucency", ui.translucency),
    FIELD_ROW("windowLayout", ui.windowLayout),
    FIELD_ROW("roundedCorners", ui.roundedCorners),
    FIELD_ROW("boringMode", ui.boringMode),
    // General shows it only where Speecher can start at login
    // (System.canLaunchAtLogin).
    FIELD_ROW("launchAtLogin", launchAtLogin),
    // What Speecher Keeps, by historyKeepName; the days as option ids.
    {"historyKeeps", [](const AppSettings &s) { return QVariant(historyKeepName(s.history.keeps)); },
     [](AppSettings &s, const QVariant &v) {
         s.history.keeps = historyKeepFromName(v.toString()).value_or(s.history.keeps);
     }},
    {"historyTextDays", [](const AppSettings &s) { return QVariant(QString::number(s.history.textDays)); },
     [](AppSettings &s, const QVariant &v) { s.history.textDays = qMax(0, v.toInt()); }},
    {"historyAudioDays", [](const AppSettings &s) { return QVariant(QString::number(s.history.audioDays)); },
     [](AppSettings &s, const QVariant &v) { s.history.audioDays = qMax(-1, v.toInt()); }},
    // The Recordings page's rows. The service is { service, model }, or
    // empty for the Service Order's; minutes are option ids.
    {"recordingSources", [](const AppSettings &s) { return QVariant(s.recording.systemAudio ? "both" : "mic"); },
     [](AppSettings &s, const QVariant &v) { s.recording.systemAudio = v.toString() != QLatin1String("mic"); }},
    {"recordingService",
     [](const AppSettings &s) {
         return s.recording.first ? QVariant(QVariantMap{{QStringLiteral("service"), s.recording.first->service},
                                                         {QStringLiteral("model"), s.recording.first->model}})
                                  : QVariant(QVariantMap());
     },
     [](AppSettings &s, const QVariant &v) {
         const QVariantMap first = v.toMap();
         const QString service = first.value(QStringLiteral("service")).toString();
         s.recording.first = service.isEmpty()
             ? std::nullopt
             : std::optional(ServiceChoice{service, first.value(QStringLiteral("model")).toString()});
     }},
    FIELD_ROW("recordingProfile", recording.writingProfile),
    FIELD_ROW("recordingFolder", recording.folder),
    {"recordingFormat",
     [](const AppSettings &s) { return QVariant(s.recording.format == RecordingFormat::PlainText ? "txt" : "md"); },
     [](AppSettings &s, const QVariant &v) {
         s.recording.format = v.toString() == QLatin1String("txt") ? RecordingFormat::PlainText : RecordingFormat::Markdown;
     }},
    FIELD_ROW("recordingKeepAudio", recording.keepAudio),
    {"recordingAutoStop", [](const AppSettings &s) { return QVariant(QString::number(s.recording.autoStopMinutes)); },
     [](AppSettings &s, const QVariant &v) { s.recording.autoStopMinutes = qMax(0, v.toInt()); }},
    FIELD_ROW("recordingPill", recording.showPill),

    {"updateChannel", [](const AppSettings &s) { return QVariant(updateChannelName(s.updates.channel)); },
     [](AppSettings &s, const QVariant &v) { s.updates.channel = updateChannelFromName(v.toString()); }},
    FIELD_ROW("autoCheckUpdates", updates.autoCheck),
    FIELD_ROW("autoInstallUpdates", updates.autoInstall),
    // A listed frequency is its minutes; an interval given in a unit, or one
    // not listed, is "custom", and choosing Custom keeps the interval for the
    // custom row.
    {"updateCheckInterval",
     [](const AppSettings &s) {
         const bool listed = s.updates.checkIntervalUnit.isEmpty()
             && std::ranges::find(listedCheckIntervals, s.updates.checkIntervalMinutes) != std::end(listedCheckIntervals);
         return QVariant(listed ? QString::number(s.updates.checkIntervalMinutes) : QStringLiteral("custom"));
     },
     [](AppSettings &s, const QVariant &v) {
         if (v.toString() == QLatin1String("custom")) {
             s.updates.checkIntervalUnit = checkIntervalUnit(s.updates);
             return;
         }
         bool isNumber = false;
         const int minutes = v.toString().toInt(&isNumber);
         if (!isNumber) {
             qWarning().noquote() << "Settings.set(updateCheckInterval) got no number of minutes:" << v;
             return;
         }
         s.updates.checkIntervalUnit.clear();
         s.updates.checkIntervalMinutes =
             qBound(UpdateSettings::minimumCheckIntervalMinutes, minutes, UpdateSettings::maximumCheckIntervalMinutes);
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
    {"pasteAgainShortcut", GlobalShortcutRole::PasteAgain},
    {"recordingShortcut", GlobalShortcutRole::Recording},
};

const QString runSetupAction = QStringLiteral("runSetup");
// Points the refinement server at the CLI Proxy API server the sign-in names.
const QString useCliproxyServerAction = QStringLiteral("useCliproxyServer");

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
    SettingsStore *store = controller->settings();
    connect(store, &SettingsStore::snapshotApplied, this, &QuickSettings::refresh);
    // LocalSetup's live facts, such as endpoint checks and detected runners,
    // change without a save.
    connect(controller->localSetup(), &LocalSetup::changed, this, &QuickSettings::refresh);
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
    if (row->refuse) {
        const QString refusal = row->refuse(value);
        setError(id, refusal);
        if (!refusal.isEmpty()) {
            return;
        }
    }
    const SettingsEdit write = [this, row, value](AppSettings &settings) {
        if (row->writeHere) {
            row->writeHere(settings, value, m_controller->localSetup()->liveFacts(settings));
        } else {
            row->write(settings, value);
        }
    };
    if (m_draftRows.contains(id)) {
        // A Service Order waits with only what this edit changed, so a model
        // saved meanwhile (a download, a runner's pull, a Fix sheet) stays.
        const bool order = id == u"speechServices" || id == u"refinementServices";
        const SettingsEdit drafted = order ? SettingsEdit([row, value = changedFields(row->read(shown()), value)](
                                                              AppSettings &settings) { row->write(settings, value); })
                                           : write;
        m_draft = [before = m_draft, drafted](AppSettings &settings) {
            if (before) {
                before(settings);
            }
            drafted(settings);
        };
        refresh();
        return;
    }
    m_controller->settings()->edit([&](AppSettings &draft) {
        write(draft);
        return true;
    });
}

void QuickSettings::startDraft(const QStringList &ids)
{
    for (const QString &id : ids) {
        if (!findRow(settingRows, id)) {
            qWarning().noquote() << "Settings.startDraft:" << id << "is not a row the UI saves";
        }
    }
    m_draftRows = ids;
    m_draft = {};
    refresh();
}

void QuickSettings::saveDraft()
{
    const SettingsEdit draft = std::exchange(m_draft, {});
    m_draftRows.clear();
    if (draft) {
        m_controller->settings()->edit([&draft](AppSettings &settings) {
            draft(settings);
            return true;
        });
    }
    refresh();
}

void QuickSettings::dropDraft()
{
    m_draftRows.clear();
    m_draft = {};
    refresh();
}

AppSettings QuickSettings::shown() const
{
    AppSettings settings = m_controller->settings()->dictationSnapshot();
    if (m_draft) {
        m_draft(settings);
    }
    return settings;
}

void QuickSettings::act(const QString &id)
{
    if (id == runSetupAction) {
        m_controller->showSetup();
        return;
    }
    if (id == useCliproxyServerAction) {
        m_controller->settings()->edit([](AppSettings &draft) {
            useCliproxyServer(draft);
            return true;
        });
        return;
    }
    if (desktopPicksKeys(id)) {
        setError(id, {});
        m_controller->registerGlobalShortcut(findRow(shortcutRows, id)->role);
        return;
    }
    qWarning().noquote() << "Settings.act:" << id << "is not an action the UI runs yet";
}

void QuickSettings::completeSetup()
{
    m_controller->completeSetup();
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
        QCoreApplication::instance()->installEventFilter(this);
    }
}

void QuickSettings::stopRecordingShortcut(const QString &id)
{
    if (id != m_recordingShortcut) {
        return;
    }
    m_recordingShortcut.clear();
    QCoreApplication::instance()->removeEventFilter(this);
    m_lastKeyCode.clear();
    GlobalShortcutRole failedRole = GlobalShortcutRole::Dictation;
    const QString error = m_controller->resumeGlobalShortcut(&failedRole);
    if (!error.isEmpty()) {
        setError(shortcutRowId(failedRole), error);
    }
}

QStringList QuickSettings::lastKeyAlone() const
{
    const ShortcutBinding key = ShortcutBinding::singleKey(m_lastKeyCode);
    return key.isEmpty() ? QStringList() : QStringList{key.toString()};
}

bool QuickSettings::takesKeyAlone(const QString &id, const QStringList &keys)
{
    const QString refusal = singleKeyRecordingRefusal(ShortcutBinding::fromString(keys.join(QLatin1Char('+'))));
    if (!refusal.isEmpty()) {
        setError(id, refusal);
    }
    return refusal.isEmpty();
}

// Sees each key press and release before the field does, with the native
// codes QML leaves out.
bool QuickSettings::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const PhysicalKey *key = physicalKeyOf(*static_cast<QKeyEvent *>(event));
        m_lastKeyCode = key ? QString::fromLatin1(key->code) : QString();
    }
    return QObject::eventFilter(watched, event);
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
    const AppSettings settings = shown();
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
