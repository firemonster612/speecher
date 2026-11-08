#include "frontend/quick/host/QuickApps.h"

#include "app/ApplicationController.h"
#include "core/AppEdits.h"
#include "core/SeenApps.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"
#include "frontend/quick/host/QmlValues.h"

#include <QDebug>
#include <QGuiApplication>
#include <QThreadPool>

#include <algorithm>

namespace speecher {

namespace {

// How long Switch to it waits for another app to come to the front once the
// countdown is over, and then for that app's window to settle.
constexpr int captureWaitSeconds = 10;
constexpr int settleMs = 700;

QThreadPool &scanPool()
{
    static QThreadPool pool;
    return pool;
}

QString layerName(AppLayer layer)
{
    switch (layer) {
    case AppLayer::App:
        return QStringLiteral("app");
    case AppLayer::BuiltIn:
        return QStringLiteral("builtIn");
    case AppLayer::Terminal:
        return QStringLiteral("terminal");
    case AppLayer::Type:
        return QStringLiteral("type");
    case AppLayer::Fallback:
        break;
    }
    return QStringLiteral("fallback");
}

QString typeName(const std::optional<AppCategory> &type)
{
    return type ? appCategoryName(*type) : QString();
}

// Delivery Steps as the UI reads and edits them: { insert, press, keep }.
QVariantMap stepsRecord(const DeliverySteps &steps)
{
    return {{QStringLiteral("insert"), steps.insert}, {QStringLiteral("press"), steps.press},
            {QStringLiteral("keep"), steps.keep}};
}

// Steps of an App's or App Type's own, or null where it has none.
QVariant ownSteps(const std::optional<DeliverySteps> &steps)
{
    return steps ? QVariant(stepsRecord(*steps)) : QVariant();
}

QString keysName(const std::optional<PasteKeyCombo> &keys)
{
    return keys ? pasteKeyComboName(*keys) : QString();
}

// An edit's steps: null, or a map without them, unsets them.
std::optional<DeliverySteps> stepsFrom(const QVariant &value)
{
    const QVariantMap map = value.toMap();
    if (map.isEmpty()) {
        return std::nullopt;
    }
    return DeliverySteps{map.value(QStringLiteral("insert")).toBool(), map.value(QStringLiteral("press")).toBool(),
                         map.value(QStringLiteral("keep")).toBool()};
}

// Applies the pasting parts of an App or App Type edit: steps and keys, each
// unset by an empty value.
void editPasting(PastingChoice &pasting, const QVariantMap &patch)
{
    if (patch.contains(QStringLiteral("steps"))) {
        pasting.steps = stepsFrom(patch.value(QStringLiteral("steps")));
    }
    if (patch.contains(QStringLiteral("keys"))) {
        const QString keys = patch.value(QStringLiteral("keys")).toString();
        pasting.keys = keys.isEmpty() ? std::nullopt : std::optional(pasteKeyComboFromName(keys));
    }
}

QString historyName(const std::optional<HistoryKeep> &history)
{
    return history ? historyKeepName(*history) : QString();
}

// An App's or App Type's History choice from an edit: empty unsets it, and
// only Stats and Nothing are choices, as one can only lower What Speecher
// Keeps.
std::optional<std::optional<HistoryKeep>> historyChoice(const QVariant &value)
{
    const QString text = value.toString();
    if (text.isEmpty()) {
        return std::optional<HistoryKeep>();
    }
    const std::optional<HistoryKeep> keep = historyKeepFromName(text);
    if (!keep || *keep > HistoryKeep::Stats) {
        return std::nullopt;
    }
    return keep;
}

QString kindName(AppMatchKind kind)
{
    return kind == AppMatchKind::Id ? QStringLiteral("id")
         : kind == AppMatchKind::Name ? QStringLiteral("name") : QStringLiteral("title");
}

// A Target as the app would report itself, from what its App matches, for
// showing what core makes of it.
Target targetFor(const AppEntry &app)
{
    Target target;
    for (const AppMatch &match : app.matches) {
        QString &field = match.kind == AppMatchKind::Id     ? target.applicationId
                       : match.kind == AppMatchKind::Name   ? target.applicationName
                                                            : target.windowTitle;
        if (field.isEmpty()) {
            field = match.value;
        }
    }
    return target;
}

Target targetFor(const InstalledApp &app)
{
    Target target;
    target.applicationId = app.ids.value(0);
    target.applicationName = app.name;
    target.processName = app.ids.value(app.ids.size() - 1);
    return target;
}

// What a delivery to a Target resolved this way does, with the Pasting
// page's steps: { steps, keys }.
QVariantMap planRecord(const DeliverySteps &steps, const PastingChoice &pasting)
{
    const DeliveryPlan plan = deliveryPlan(steps, pasting);
    return {{QStringLiteral("steps"), stepsRecord(plan.steps)}, {QStringLiteral("keys"), pasteKeyComboName(plan.keys)}};
}

QVariantMap resolved(const AppResolution &resolution, const DeliverySteps &steps)
{
    QVariantMap record = planRecord(steps, resolution.pasting);
    record.insert(QStringLiteral("type"), appCategoryName(resolution.type));
    record.insert(QStringLiteral("profile"), resolution.writingProfile);
    record.insert(QStringLiteral("history"), historyName(resolution.history));
    return record;
}

// What core makes of the App, now and with each of its own values unset in
// turn, with the Pasting page's steps.
QVariantMap entryRecord(const AppEntry &app, const AppsSettings &apps, const DeliverySteps &steps)
{
    const Target target = targetFor(app);
    const auto without = [&](auto clear) {
        AppsSettings changed = apps;
        for (AppEntry &entry : changed.apps) {
            if (entry.id == app.id) {
                clear(entry);
            }
        }
        return resolveApp(target, changed);
    };
    const AppResolution type = without([](AppEntry &entry) { entry.type.reset(); });
    const AppResolution profile = without([](AppEntry &entry) { entry.writingProfile.reset(); });
    const AppResolution pasting = without([](AppEntry &entry) { entry.pasting = {}; });
    const AppResolution history = without([](AppEntry &entry) { entry.history.reset(); });
    QStringList ids;
    QVariantList matches;
    bool byText = true;
    bool titleOnly = true;
    for (const AppMatch &match : app.matches) {
        ids.append(match.value);
        matches.append(QVariantMap{{QStringLiteral("kind"), kindName(match.kind)}, {QStringLiteral("value"), match.value}});
        byText = byText && match.kind != AppMatchKind::Id;
        titleOnly = titleOnly && match.kind == AppMatchKind::WindowTitle;
    }
    ids.removeDuplicates();
    return {
        {QStringLiteral("id"), app.id},
        {QStringLiteral("name"), app.name},
        {QStringLiteral("icon"), app.icon},
        {QStringLiteral("ids"), ids},
        {QStringLiteral("matches"), matches},
        {QStringLiteral("byText"), byText},
        {QStringLiteral("titleOnly"), titleOnly},
        {QStringLiteral("type"), typeName(app.type)},
        {QStringLiteral("profile"), app.writingProfile.value_or(QString())},
        {QStringLiteral("steps"), ownSteps(app.pasting.steps)},
        {QStringLiteral("keys"), keysName(app.pasting.keys)},
        {QStringLiteral("history"), historyName(app.history)},
        {QStringLiteral("effective"), resolved(resolveApp(target, apps), steps)},
        {QStringLiteral("inherited"),
         QVariantMap{{QStringLiteral("type"), appCategoryName(type.type)},
                     {QStringLiteral("profile"), profile.writingProfile},
                     {QStringLiteral("profileFrom"), layerName(profile.writingProfileFrom)},
                     {QStringLiteral("steps"), ownSteps(pasting.pasting.steps)},
                     {QStringLiteral("stepsFrom"), layerName(pasting.stepsFrom)},
                     {QStringLiteral("keys"), keysName(pasting.pasting.keys)},
                     {QStringLiteral("keysFrom"), layerName(pasting.keysFrom)},
                     {QStringLiteral("history"), historyName(history.history)},
                     {QStringLiteral("historyFrom"), layerName(history.historyFrom)}}},
    };
}

// The installed app one of whose ids is id, for its icon and name.
const InstalledApp *installedWithId(const QList<InstalledApp> &installed, const QString &id)
{
    const QString wanted = comparableAppId(id);
    for (const InstalledApp &app : installed) {
        if (std::any_of(app.ids.cbegin(), app.ids.cend(),
                        [&wanted](const QString &own) { return comparableAppId(own) == wanted; })) {
            return &app;
        }
    }
    return nullptr;
}

// The matches of an add or edit: [{ kind: "id" | "name" | "title", value }].
// Nullopt, with a warning, for a kind that isn't one.
std::optional<QList<AppMatch>> matchesFrom(const QVariantList &values)
{
    QList<AppMatch> matches;
    for (const QVariant &value : values) {
        const QVariantMap match = value.toMap();
        const QString kind = match.value(QStringLiteral("kind")).toString();
        if (kind != u"id" && kind != u"name" && kind != u"title") {
            qWarning().noquote() << "Apps takes no match of kind" << kind;
            return std::nullopt;
        }
        matches.append({kind == u"id" ? AppMatchKind::Id : kind == u"name" ? AppMatchKind::Name : AppMatchKind::WindowTitle,
                        match.value(QStringLiteral("value")).toString()});
    }
    return matches;
}

// Where a resolved value came from, for Test: { layer, app } with the id of
// the App that set it, the first of matched to set it, where layer is app.
QVariantMap sourceRecord(AppLayer layer, const QStringList &matched, const AppsSettings &apps,
                         const std::function<bool(const AppEntry &)> &sets)
{
    QString app;
    if (layer == AppLayer::App) {
        for (const QString &id : matched) {
            const auto entry = std::find_if(apps.apps.cbegin(), apps.apps.cend(),
                                            [&id](const AppEntry &each) { return each.id == id; });
            if (entry != apps.apps.cend() && sets(*entry)) {
                app = id;
                break;
            }
        }
    }
    return {{QStringLiteral("layer"), layerName(layer)}, {QStringLiteral("app"), app}};
}

// An App edit's field: empty unsets it.
template <typename T, typename Parse>
std::optional<T> optionalFrom(const QVariant &value, Parse parse)
{
    const QString text = value.toString();
    return text.isEmpty() ? std::nullopt : std::optional<T>(parse(text));
}

} // namespace

QuickApps::QuickApps(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickApps::rebuild);
    connect(controller->seenApps(), &SeenApps::changed, this, &QuickApps::rebuild);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_countdown > 0) {
            --m_countdown;
            emit captureChanged();
            return;
        }
        // Past the countdown, as long as Speecher is still in front.
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
            m_tick.stop();
            QTimer::singleShot(settleMs, this, &QuickApps::captureFront);
        } else if (--m_countdown < -captureWaitSeconds) {
            m_tick.stop();
            setCapture(QStringLiteral("failed"));
        }
    });
    rebuild();
}

QuickApps::~QuickApps()
{
    // A scan reports back to this object; it must not outlive it.
    scanPool().waitForDone();
}

int QuickApps::builtInCount() const
{
    return int(builtInApps().size());
}

void QuickApps::refresh()
{
    if (m_scanned || m_scanning) {
        return;
    }
    m_scanning = true;
    emit installedChanged();
    scanPool().start([this] {
        QList<InstalledApp> apps = installedApps();
        QMetaObject::invokeMethod(this, [this, apps = std::move(apps)] {
            m_installed = apps;
            m_scanning = false;
            m_scanned = true;
            rebuild();
        }, Qt::QueuedConnection);
    });
}

void QuickApps::rebuild()
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const AppsSettings &apps = settings.apps;

    QVariantList entries;
    for (const AppEntry &app : apps.apps) {
        entries.append(entryRecord(app, apps, settings.output.steps));
    }
    QVariantList types;
    for (const AppTypeSettings &type : apps.types) {
        const bool other = type.type == AppCategory::General;
        types.append(QVariantMap{
            {QStringLiteral("id"), appCategoryName(type.type)},
            {QStringLiteral("profile"), other ? fallbackWritingProfile(apps) : type.writingProfile.value_or(QString())},
            {QStringLiteral("steps"), ownSteps(type.pasting.steps)},
            {QStringLiteral("keys"), keysName(type.pasting.keys)},
            {QStringLiteral("history"), historyName(type.history)},
            {QStringLiteral("effectiveProfile"), type.writingProfile.value_or(fallbackWritingProfile(apps))},
            {QStringLiteral("effective"), planRecord(settings.output.steps, type.pasting)},
        });
    }
    QVariantList recent;
    for (const SeenApp &seen : m_controller->seenApps()->apps()) {
        Target target;
        target.applicationId = seen.applicationId;
        target.applicationName = seen.name;
        const AppResolution resolution = resolveApp(target, apps);
        const InstalledApp *installed = installedWithId(m_installed, seen.applicationId);
        recent.append(QVariantMap{
            {QStringLiteral("name"), installed ? installed->name : seen.name.isEmpty() ? seen.applicationId : seen.name},
            {QStringLiteral("appId"), seen.applicationId},
            {QStringLiteral("icon"), installed ? installed->icon : QString()},
            {QStringLiteral("lastSeenMs"), double(seen.lastSeenMs)},
            {QStringLiteral("dictations"), seen.dictations},
            {QStringLiteral("entry"), resolution.appId},
            {QStringLiteral("type"), appCategoryName(resolution.type)},
            {QStringLiteral("profile"), resolution.writingProfile},
        });
    }
    if (entries != m_entries || types != m_types || recent != m_recent) {
        m_entries = entries;
        m_types = types;
        m_recent = recent;
        emit changed();
    }
    rebuildInstalled();
}

void QuickApps::rebuildInstalled()
{
    const AppsSettings apps = m_controller->settings()->dictationSnapshot().apps;
    // Launchers of one program are one app to an Id match, so they list as
    // one, under the first one's name.
    QList<QStringList> idLists;
    for (const InstalledApp &app : m_installed) {
        idLists.append(app.ids);
    }
    QVariantList records;
    for (const QList<int> &group : sharedIdGroups(idLists)) {
        const InstalledApp &first = m_installed.at(group.first());
        QStringList ids;
        QStringList also;
        for (const int index : group) {
            const InstalledApp &app = m_installed.at(index);
            for (const QString &id : app.ids) {
                if (std::none_of(ids.cbegin(), ids.cend(),
                                 [&id](const QString &own) { return comparableAppId(own) == comparableAppId(id); })) {
                    ids.append(id);
                }
            }
            if (index != group.first() && app.name != first.name && !also.contains(app.name)) {
                also.append(app.name);
            }
        }
        InstalledApp grouped = first;
        grouped.ids = ids;
        const AppResolution resolution = resolveApp(targetFor(grouped), apps);
        records.append(QVariantMap{
            {QStringLiteral("name"), first.name},
            {QStringLiteral("icon"), first.icon},
            {QStringLiteral("ids"), ids},
            {QStringLiteral("also"), also},
            {QStringLiteral("entry"), resolution.appId},
            {QStringLiteral("type"), appCategoryName(resolution.type)},
            {QStringLiteral("profile"), resolution.writingProfile},
        });
    }
    if (records != m_installedRecords) {
        m_installedRecords = records;
        emit installedChanged();
    }
}

QString QuickApps::saveUnlessRefused(const std::function<EditRefusal(AppSettings &)> &edit)
{
    EditRefusal refusal = EditRefusal::None;
    m_controller->settings()->edit([&](AppSettings &s) { return (refusal = edit(s)) == EditRefusal::None; });
    return refusalName(refusal);
}

QVariantMap QuickApps::add(const QVariantMap &app)
{
    AppEntry entry;
    entry.name = app.value(QStringLiteral("name")).toString();
    entry.icon = app.value(QStringLiteral("icon")).toString();
    for (const QString &id : app.value(QStringLiteral("ids")).toStringList()) {
        entry.matches.append({AppMatchKind::Id, id});
    }
    const std::optional<QList<AppMatch>> matches = matchesFrom(app.value(QStringLiteral("matches")).toList());
    if (!matches) {
        return {{QStringLiteral("id"), QString()}, {QStringLiteral("refusal"), invalidRecord}};
    }
    entry.matches += *matches;
    QStringList taken;
    for (const QVariant &existing : std::as_const(m_entries)) {
        taken.append(existing.toMap().value(QStringLiteral("id")).toString());
    }
    entry.id = newAppId(taken);
    const QString refusal = saveUnlessRefused([&entry](AppSettings &s) { return addApp(s, entry); });
    return {{QStringLiteral("id"), refusal.isEmpty() ? entry.id : QString()}, {QStringLiteral("refusal"), refusal}};
}

QString QuickApps::update(const QString &id, const QVariantMap &patch)
{
    const auto history = historyChoice(patch.value(QStringLiteral("history")));
    if (!history) {
        qWarning().noquote() << "Apps.update takes no History choice" << patch.value(QStringLiteral("history")).toString();
        return invalidRecord;
    }
    const std::optional<QList<AppMatch>> matches = matchesFrom(patch.value(QStringLiteral("matches")).toList());
    if (!matches) {
        return invalidRecord;
    }
    return saveUnlessRefused([&](AppSettings &s) {
        return editApp(s, id, [&patch, &history, &matches](AppEntry &app) {
            editPasting(app.pasting, patch);
            if (patch.contains(QStringLiteral("matches"))) {
                app.matches = *matches;
            }
            for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
                if (it.key() == u"type") {
                    app.type = optionalFrom<AppCategory>(it.value(), appCategoryFromName);
                } else if (it.key() == u"profile") {
                    app.writingProfile = optionalFrom<QString>(it.value(), [](const QString &v) { return v; });
                } else if (it.key() == u"history") {
                    app.history = *history;
                } else if (it.key() == u"name") {
                    app.name = it.value().toString();
                }
            }
        });
    });
}

int QuickApps::remove(const QString &id)
{
    SettingsEdit undo;
    m_controller->settings()->edit([&](AppSettings &s) { return bool(undo = removeApp(s, id)); });
    if (!undo) {
        return -1;
    }
    m_undo = undo;
    return ++m_lastUndo;
}

void QuickApps::undo(int token)
{
    if (token != m_lastUndo || !m_undo) {
        return;
    }
    const SettingsEdit edit = std::exchange(m_undo, {});
    m_controller->settings()->edit([&edit](AppSettings &s) {
        edit(s);
        return true;
    });
}

QString QuickApps::updateType(const QString &type, const QVariantMap &patch)
{
    const auto history = historyChoice(patch.value(QStringLiteral("history")));
    if (!history) {
        qWarning().noquote() << "Apps.updateType takes no History choice" << patch.value(QStringLiteral("history")).toString();
        return invalidRecord;
    }
    return saveUnlessRefused([&](AppSettings &s) {
        return editAppType(s, appCategoryFromName(type), [&patch, &history](AppTypeSettings &settings) {
            if (patch.contains(QStringLiteral("profile"))) {
                settings.writingProfile =
                    optionalFrom<QString>(patch.value(QStringLiteral("profile")), [](const QString &v) { return v; });
            }
            editPasting(settings.pasting, patch);
            if (patch.contains(QStringLiteral("history"))) {
                settings.history = *history;
            }
        });
    });
}

void QuickApps::setCapture(const QString &state)
{
    if (m_capture != state) {
        m_capture = state;
        emit captureChanged();
    }
}

void QuickApps::captureNext(int seconds)
{
    m_testing = false;
    startCapture(seconds);
}

void QuickApps::testNext(int seconds)
{
    m_testing = true;
    m_tested.clear();
    emit testedChanged();
    startCapture(seconds);
}

void QuickApps::startCapture(int seconds)
{
    m_countdown = std::max(0, seconds);
    m_capture.clear();
    setCapture(QStringLiteral("waiting"));
    m_tick.start();
}

void QuickApps::cancelCapture()
{
    m_tick.stop();
    m_countdown = 0;
    setCapture({});
}

void QuickApps::captureFront()
{
    TargetProvider *provider = m_controller->targetProvider();
    // Capturing drops what a dictation under way captured.
    if (m_capture != u"waiting" || !provider || m_controller->dictationState() != DictationState::Idle) {
        setCapture(QStringLiteral("failed"));
        return;
    }
    provider->capture(this, [this](const Target &target) {
        if (m_capture != u"waiting") {
            return;
        }
        if (m_testing) {
            showTested(target);
            return;
        }
        if (target.applicationId.trimmed().isEmpty()) {
            setCapture(QStringLiteral("failed"));
            return;
        }
        QString id = resolveApp(target, m_controller->settings()->dictationSnapshot().apps).appId;
        if (id.isEmpty()) {
            QStringList ids{target.applicationId};
            if (!target.processName.isEmpty()
                && comparableAppId(target.processName) != comparableAppId(target.applicationId)) {
                ids.append(target.processName);
            }
            const QString name = !target.applicationName.isEmpty() ? target.applicationName : target.applicationId;
            id = add({{QStringLiteral("name"), name}, {QStringLiteral("ids"), ids}}).value(QStringLiteral("id")).toString();
        }
        setCapture(id.isEmpty() ? QStringLiteral("failed") : QString());
        if (!id.isEmpty()) {
            emit captured(id);
        }
    });
}

void QuickApps::showTested(const Target &target)
{
    if (!target.hasIdentity()) {
        setCapture(QStringLiteral("failed"));
        return;
    }
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const AppsSettings &apps = settings.apps;
    const AppResolution resolution = resolveApp(target, apps);
    const QStringList matched = matchingAppIds(target, apps);
    const auto source = [&](AppLayer layer, auto sets) { return sourceRecord(layer, matched, apps, sets); };
    QVariantMap tested = resolved(resolution, settings.output.steps);
    tested.insert(QStringLiteral("name"), !target.applicationName.isEmpty() ? target.applicationName : target.applicationId);
    tested.insert(QStringLiteral("appId"), target.applicationId);
    tested.insert(QStringLiteral("windowTitle"), target.windowTitle);
    tested.insert(QStringLiteral("matched"), matched);
    tested.insert(QStringLiteral("from"), QVariantMap{
        {QStringLiteral("profile"), source(resolution.writingProfileFrom, [](const AppEntry &app) { return bool(app.writingProfile); })},
        {QStringLiteral("steps"), source(resolution.stepsFrom, [](const AppEntry &app) { return bool(app.pasting.steps); })},
        {QStringLiteral("keys"), source(resolution.keysFrom, [](const AppEntry &app) { return bool(app.pasting.keys); })},
        {QStringLiteral("history"), source(resolution.historyFrom, [](const AppEntry &app) { return bool(app.history); })},
    });
    m_tested = tested;
    setCapture({});
    emit testedChanged();
}

} // namespace speecher
