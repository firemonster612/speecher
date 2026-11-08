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

QString pastingName(const std::optional<PasteMethod> &pasting)
{
    return pasting ? pasteMethodName(*pasting) : QString();
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

QVariantMap resolved(const AppResolution &resolution)
{
    return {{QStringLiteral("type"), appCategoryName(resolution.type)},
            {QStringLiteral("profile"), resolution.writingProfile},
            {QStringLiteral("pasting"), pastingName(resolution.pasting)}};
}

// What core makes of the App, now and with each of its own values unset in
// turn.
QVariantMap entryRecord(const AppEntry &app, const AppsSettings &apps)
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
    const AppResolution pasting = without([](AppEntry &entry) { entry.pasting.reset(); });
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
        {QStringLiteral("pasting"), pastingName(app.pasting)},
        {QStringLiteral("effective"), resolved(resolveApp(target, apps))},
        {QStringLiteral("inherited"),
         QVariantMap{{QStringLiteral("type"), appCategoryName(type.type)},
                     {QStringLiteral("profile"), profile.writingProfile},
                     {QStringLiteral("profileFrom"), layerName(profile.writingProfileFrom)},
                     {QStringLiteral("pasting"), pastingName(pasting.pasting)},
                     {QStringLiteral("pastingFrom"), layerName(pasting.pastingFrom)}}},
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
        entries.append(entryRecord(app, apps));
    }
    QVariantList types;
    for (const AppTypeSettings &type : apps.types) {
        const bool other = type.type == AppCategory::General;
        types.append(QVariantMap{
            {QStringLiteral("id"), appCategoryName(type.type)},
            {QStringLiteral("profile"), other ? fallbackWritingProfile(apps) : type.writingProfile.value_or(QString())},
            {QStringLiteral("pasting"), pastingName(type.pasting)},
            {QStringLiteral("effectiveProfile"), type.writingProfile.value_or(fallbackWritingProfile(apps))},
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
    QVariantList records;
    for (const InstalledApp &app : m_installed) {
        const AppResolution resolution = resolveApp(targetFor(app), apps);
        records.append(QVariantMap{
            {QStringLiteral("name"), app.name},
            {QStringLiteral("icon"), app.icon},
            {QStringLiteral("ids"), app.ids},
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
    for (const QVariant &value : app.value(QStringLiteral("matches")).toList()) {
        const QVariantMap match = value.toMap();
        const QString kind = match.value(QStringLiteral("kind")).toString();
        if (kind != u"name" && kind != u"title") {
            qWarning().noquote() << "Apps.add takes no match of kind" << kind;
            return {{QStringLiteral("id"), QString()}, {QStringLiteral("refusal"), invalidRecord}};
        }
        entry.matches.append({kind == u"name" ? AppMatchKind::Name : AppMatchKind::WindowTitle,
                              match.value(QStringLiteral("value")).toString()});
    }
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
    return saveUnlessRefused([&](AppSettings &s) {
        return editApp(s, id, [&patch](AppEntry &app) {
            for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
                if (it.key() == u"type") {
                    app.type = optionalFrom<AppCategory>(it.value(), appCategoryFromName);
                } else if (it.key() == u"profile") {
                    app.writingProfile = optionalFrom<QString>(it.value(), [](const QString &v) { return v; });
                } else if (it.key() == u"pasting") {
                    app.pasting = optionalFrom<PasteMethod>(it.value(), pasteMethodFromName);
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
    return saveUnlessRefused([&](AppSettings &s) {
        return editAppType(s, appCategoryFromName(type), [&patch](AppTypeSettings &settings) {
            if (patch.contains(QStringLiteral("profile"))) {
                settings.writingProfile =
                    optionalFrom<QString>(patch.value(QStringLiteral("profile")), [](const QString &v) { return v; });
            }
            if (patch.contains(QStringLiteral("pasting"))) {
                settings.pasting = optionalFrom<PasteMethod>(patch.value(QStringLiteral("pasting")), pasteMethodFromName);
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

} // namespace speecher
