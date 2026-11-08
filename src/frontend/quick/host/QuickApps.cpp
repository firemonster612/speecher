#include "frontend/quick/host/QuickApps.h"

#include "app/ApplicationController.h"
#include "core/AppEdits.h"
#include "core/SeenApps.h"
#include "core/SettingsStore.h"
#include "dictation/DictationTypes.h"
#include "frontend/quick/host/QmlValues.h"

#include <QDebug>
#include <QGuiApplication>
#include <QThreadPool>

#include <algorithm>

namespace speecher {

namespace {

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
    const AppResolution inherited = inheritedFor(app, apps);
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
        {QStringLiteral("effective"), resolved(resolveApp(targetFor(app), apps), steps)},
        {QStringLiteral("inherited"),
         QVariantMap{{QStringLiteral("type"), appCategoryName(inherited.type)},
                     {QStringLiteral("profile"), inherited.writingProfile},
                     {QStringLiteral("profileFrom"), layerName(inherited.writingProfileFrom)},
                     {QStringLiteral("steps"), ownSteps(inherited.pasting.steps)},
                     {QStringLiteral("stepsFrom"), layerName(inherited.stepsFrom)},
                     {QStringLiteral("keys"), keysName(inherited.pasting.keys)},
                     {QStringLiteral("keysFrom"), layerName(inherited.keysFrom)},
                     {QStringLiteral("history"), historyName(inherited.history)},
                     {QStringLiteral("historyFrom"), layerName(inherited.historyFrom)}}},
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

// Where a resolved value came from, for Test: { layer, app }, with the id of
// the App that set it where layer is app.
QVariantMap sourceRecord(AppLayer layer, const QString &app)
{
    return {{QStringLiteral("layer"), layerName(layer)}, {QStringLiteral("app"), app}};
}

// An edit's text field: empty unsets it.
std::optional<QString> nonEmpty(const QVariant &value)
{
    const QString text = value.toString();
    return text.isEmpty() ? std::nullopt : std::optional(text);
}

} // namespace

QuickApps::QuickApps(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_capture(
          controller->targetProvider(),
          [controller] { return controller->dictationState() != DictationState::Idle; },
          [] { return QGuiApplication::applicationState() == Qt::ApplicationActive; })
{
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickApps::rebuild);
    connect(controller->seenApps(), &SeenApps::changed, this, &QuickApps::rebuild);
    connect(&m_capture, &AppCapture::changed, this, &QuickApps::captureChanged);
    connect(&m_capture, &AppCapture::failed, this, [this] { setCaptureFailed(true); });
    connect(&m_capture, &AppCapture::captured, this, [this](const Target &target) {
        if (m_testing) {
            showTested(target);
        } else {
            addCaptured(target);
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
    emit scanningChanged();
    scanPool().start([this] {
        QList<InstalledApp> apps = installedApps();
        QMetaObject::invokeMethod(this, [this, apps = std::move(apps)] {
            m_installed = apps;
            m_scanning = false;
            m_scanned = true;
            rebuild();
            emit scanningChanged();
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
    // addApp gives the App its id.
    QString id;
    const QString refusal = saveUnlessRefused(*m_controller->settings(), [&entry, &id](AppSettings &s) {
        const EditRefusal refusal = addApp(s, entry);
        if (refusal == EditRefusal::None) {
            id = s.apps.apps.last().id;
        }
        return refusal;
    });
    return {{QStringLiteral("id"), id}, {QStringLiteral("refusal"), refusal}};
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
    return saveUnlessRefused(*m_controller->settings(), [&](AppSettings &s) {
        return editApp(s, id, [&patch, &history, &matches](AppEntry &app) {
            editPasting(app.pasting, patch);
            if (patch.contains(QStringLiteral("matches"))) {
                app.matches = *matches;
            }
            if (patch.contains(QStringLiteral("type"))) {
                const std::optional<QString> type = nonEmpty(patch.value(QStringLiteral("type")));
                app.type = type ? std::optional(appCategoryFromName(*type)) : std::nullopt;
                // A type the person chose takes that type's profile, as for
                // any App; only a migrated rule's type left it to the catalog.
                app.catalogPicksProfile = false;
            }
            if (patch.contains(QStringLiteral("profile"))) {
                app.writingProfile = nonEmpty(patch.value(QStringLiteral("profile")));
            }
            if (patch.contains(QStringLiteral("history"))) {
                app.history = *history;
            }
            if (patch.contains(QStringLiteral("name"))) {
                app.name = patch.value(QStringLiteral("name")).toString();
            }
        });
    });
}

int QuickApps::remove(const QString &id)
{
    SettingsEdit undo;
    m_controller->settings()->edit([&](AppSettings &s) { return bool(undo = removeApp(s, id)); });
    return m_undo.keep(undo);
}

void QuickApps::undo(int token)
{
    const SettingsEdit edit = m_undo.take(token);
    if (!edit) {
        return;
    }
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
    return saveUnlessRefused(*m_controller->settings(), [&](AppSettings &s) {
        return editAppType(s, appCategoryFromName(type), [&patch, &history](AppTypeSettings &settings) {
            if (patch.contains(QStringLiteral("profile"))) {
                settings.writingProfile = nonEmpty(patch.value(QStringLiteral("profile")));
            }
            editPasting(settings.pasting, patch);
            if (patch.contains(QStringLiteral("history"))) {
                settings.history = *history;
            }
        });
    });
}

QString QuickApps::capture() const
{
    return m_capture.waiting() ? QStringLiteral("waiting") : m_captureFailed ? QStringLiteral("failed") : QString();
}

void QuickApps::setCaptureFailed(bool failed)
{
    if (m_captureFailed != failed) {
        m_captureFailed = failed;
        emit captureChanged();
    }
}

void QuickApps::captureNext(int seconds)
{
    m_testing = false;
    m_captureFailed = false;
    m_capture.start(seconds);
}

void QuickApps::testNext(int seconds)
{
    m_testing = true;
    m_captureFailed = false;
    m_tested.clear();
    emit testedChanged();
    m_capture.start(seconds);
}

void QuickApps::cancelCapture()
{
    m_captureFailed = false;
    m_capture.cancel();
}

void QuickApps::addCaptured(const Target &target)
{
    if (target.applicationId.trimmed().isEmpty()) {
        setCaptureFailed(true);
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
    if (id.isEmpty()) {
        setCaptureFailed(true);
        return;
    }
    emit captured(id);
}

void QuickApps::showTested(const Target &target)
{
    if (!target.hasIdentity()) {
        setCaptureFailed(true);
        return;
    }
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    const AppsSettings &apps = settings.apps;
    const AppResolution resolution = resolveApp(target, apps);
    const QStringList matched = matchingAppIds(target, apps);
    QVariantMap tested = resolved(resolution, settings.output.steps);
    tested.insert(QStringLiteral("name"), !target.applicationName.isEmpty() ? target.applicationName : target.applicationId);
    tested.insert(QStringLiteral("appId"), target.applicationId);
    tested.insert(QStringLiteral("windowTitle"), target.windowTitle);
    tested.insert(QStringLiteral("matched"), matched);
    tested.insert(QStringLiteral("from"), QVariantMap{
        {QStringLiteral("profile"), sourceRecord(resolution.writingProfileFrom, resolution.writingProfileApp)},
        {QStringLiteral("steps"), sourceRecord(resolution.stepsFrom, resolution.stepsApp)},
        {QStringLiteral("keys"), sourceRecord(resolution.keysFrom, resolution.keysApp)},
        {QStringLiteral("history"), sourceRecord(resolution.historyFrom, resolution.historyApp)},
    });
    m_tested = tested;
    emit testedChanged();
}

} // namespace speecher
