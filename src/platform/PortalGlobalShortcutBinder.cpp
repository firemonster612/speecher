#include "platform/PortalGlobalShortcutBinder.h"

#include "core/SettingsStore.h"
#include "platform/LinuxDesktopIntegration.h"
#include "platform/PortalRequest.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFileInfo>
#include <QUuid>

#include <utility>

namespace speecher {
namespace {

constexpr auto shortcutInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto registryInterface = "org.freedesktop.host.portal.Registry";
constexpr auto sessionInterface = "org.freedesktop.portal.Session";
constexpr auto appId = "io.github.firemonster612.speecher";
constexpr int createTimeoutMs = 5000;
constexpr int registrationTimeoutMs = 120000;

QString portalTrigger(const QKeySequence &sequence)
{
    QStringList parts = sequence.toString(QKeySequence::PortableText).split(QLatin1Char('+'));
    const QString key = parts.takeLast().toLower();
    QString trigger;
    for (const QString &part : parts) {
        if (part == QStringLiteral("Ctrl")) trigger += QStringLiteral("<Control>");
        else if (part == QStringLiteral("Alt")) trigger += QStringLiteral("<Alt>");
        else if (part == QStringLiteral("Shift")) trigger += QStringLiteral("<Shift>");
        else if (part == QStringLiteral("Meta")) trigger += QStringLiteral("<Super>");
    }
    return trigger + key;
}

bool isUnknownRegistryCall(const QDBusError &error)
{
    return error.type() == QDBusError::UnknownInterface
        || error.type() == QDBusError::UnknownMethod;
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument;
}

PortalGlobalShortcutBinder::PortalGlobalShortcutBinder(SettingsStore *settings,
                                                       GlobalShortcutAction action,
                                                       QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
    , m_settings(settings)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();

    if (!isWaylandSession()) {
        m_unsupportedReason = QStringLiteral(
            "Automatic setup needs a Wayland session.");
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) {
        m_unsupportedReason = QStringLiteral(
            "Speecher can't reach your desktop settings.");
        return;
    }

    m_supportKnown = false;
    QDBusMessage properties = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    properties.setArguments({QString::fromLatin1(shortcutInterface), QStringLiteral("version")});
    auto *supportWatcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(properties),
        this);
    connect(supportWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, supportWatcher] {
        const QDBusPendingReply<QDBusVariant> reply = *supportWatcher;
        supportWatcher->deleteLater();
        m_supportKnown = true;
        if (reply.isError()) {
            m_supported = false;
            m_unsupportedReason = QStringLiteral(
                "Your desktop can't set shortcuts for Speecher automatically.");
        } else {
            m_supported = true;
        }
        emit supportChanged();
        if (m_supported && m_bindWhenSupported) {
            m_bindWhenSupported = false;
            createSession(false);
        }
    });

    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(shortcutInterface),
        QStringLiteral("Activated"),
        this,
        SLOT(handleActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    // Deactivated arrives on key release from backends that report it (Plasma,
    // GNOME); a backend that never sends it leaves the binding a plain toggle.
    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(shortcutInterface),
        QStringLiteral("Deactivated"),
        this,
        SLOT(handleDeactivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}

bool PortalGlobalShortcutBinder::supported() const
{
    return m_supported;
}

bool PortalGlobalShortcutBinder::supportKnown() const
{
    return m_supportKnown;
}

bool PortalGlobalShortcutBinder::usesDesktopShortcutChooser() const
{
    return true;
}

QString PortalGlobalShortcutBinder::unsupportedReason() const
{
    return m_supported ? QString() : m_unsupportedReason;
}

void PortalGlobalShortcutBinder::bind()
{
    if (!m_supportKnown) {
        m_bindWhenSupported = true;
        return;
    }
    if (!m_supported) {
        return;
    }
    createSession(false);
}

void PortalGlobalShortcutBinder::registerShortcut()
{
    if (!m_supportKnown) {
        return;
    }
    if (!m_supported) {
        emit registrationFinished(false, m_unsupportedReason);
        return;
    }
    createSession(true);
}

ShortcutBinding PortalGlobalShortcutBinder::shortcut() const
{
    return m_triggerDescription.isEmpty() ? ShortcutBinding()
                                          : ShortcutBinding(GlobalShortcutBinder::defaultShortcut());
}

QString PortalGlobalShortcutBinder::shortcutDisplay() const
{
    return m_triggerDescription;
}

bool PortalGlobalShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    if (shortcut.isEmpty()) {
        return removeRegistration(error);
    }
    if (error) {
        *error = QStringLiteral(
            "Your desktop picks this key combination itself. Use Choose shortcut instead.");
    }
    return false;
}

// The portal keeps the shortcut alive as long as the session is open, so
// letting a session linger after the user switched to a single key leaves the
// old combination starting dictation. Closing the session is the removal.
bool PortalGlobalShortcutBinder::removeRegistration(QString *)
{
    m_settings->setPortalShortcutCleared(action().role, true);
    m_bindWhenSupported = false;
    // An in-flight host-identity Register keeps its watcher, whose
    // continuation would recreate the session this removal closes. Cancel
    // the continuation (the identity itself may still become ready); a
    // later bind() re-arms it.
    m_identityContinuationCancelled = m_identityPending;
    m_registrationAfterIdentity = false;
    cancelRequest();
    closePendingSession();
    if (!m_sessionPath.path().isEmpty()) {
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
            QString::fromLatin1(portalService),
            m_sessionPath.path(),
            QString::fromLatin1(sessionInterface),
            QStringLiteral("Close")));
        m_sessionPath = {};
    }
    m_triggerDescription.clear();
    emit bindingChanged();
    return true;
}

bool PortalGlobalShortcutBinder::ensureHostIdentity(bool registration)
{
    if (m_identityReady) {
        return true;
    }
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")
        || QFileInfo::exists(QStringLiteral("/.flatpak-info"))) {
        m_identityReady = true;
        return true;
    }
    if (m_identityPending) {
        // A new session request re-arms a continuation removeRegistration()
        // cancelled while Register was still in flight.
        m_identityContinuationCancelled = false;
        m_registrationAfterIdentity = m_registrationAfterIdentity || registration;
        return false;
    }

    m_identityPending = true;
    m_identityContinuationCancelled = false;
    m_registrationAfterIdentity = registration;
    QDBusMessage registrationCall = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(registryInterface),
        QStringLiteral("Register"));
    registrationCall.setArguments({QString::fromLatin1(appId), QVariantMap()});
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(registrationCall), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        const bool requestedRegistration = m_registrationAfterIdentity;
        m_identityPending = false;
        m_registrationAfterIdentity = false;
        if (reply.isError() && !isUnknownRegistryCall(reply.error())) {
            m_unsupportedReason = QStringLiteral("Your desktop turned down the request: %1")
                                      .arg(reply.error().message());
            if (requestedRegistration) {
                emit registrationFinished(false, m_unsupportedReason);
            }
            return;
        }
        m_identityReady = true;
        // removeRegistration() ran while Register was in flight: the
        // identity stays ready for later binds, but creating the session
        // now would resurrect the shortcut the removal just closed.
        if (m_identityContinuationCancelled) {
            m_identityContinuationCancelled = false;
            return;
        }
        createSession(requestedRegistration);
    });
    return false;
}

void PortalGlobalShortcutBinder::createSession(bool registration)
{
    // The portal keeps an app's bound shortcuts across sessions and
    // ListShortcuts hands them back whenever a session is restored, so a
    // shortcut the person let go of would return on the next launch, or
    // after a chooser they cancelled.
    if (!registration && m_settings->portalShortcutCleared(action().role)) {
        return;
    }
    if (!m_supported) {
        if (registration) {
            emit registrationFinished(false, m_unsupportedReason);
        }
        return;
    }
    cancelRequest();
    closePendingSession();

    if (!ensureHostIdentity(registration)) {
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"),
                   QStringLiteral("speecher_session_%1")
                       .arg(QUuid::createUuid().toString(QUuid::Id128)));
    sendRequest(QStringLiteral("CreateSession"),
                {options},
                registration ? RequestKind::CreateForRegistration
                             : RequestKind::CreateForRestore,
                createTimeoutMs);
}

void PortalGlobalShortcutBinder::listShortcuts()
{
    sendRequest(QStringLiteral("ListShortcuts"),
                {QVariant::fromValue(m_pendingSessionPath), QVariantMap()},
                RequestKind::List,
                createTimeoutMs);
}

void PortalGlobalShortcutBinder::bindShortcuts()
{
    PortalShortcut shortcut;
    shortcut.id = action().id;
    shortcut.properties.insert(QStringLiteral("description"), action().description);
    if (!action().defaultShortcut.isEmpty()) {
        shortcut.properties.insert(QStringLiteral("preferred_trigger"),
                                   portalTrigger(action().defaultShortcut));
    }
    sendRequest(QStringLiteral("BindShortcuts"),
                {QVariant::fromValue(m_pendingSessionPath),
                 QVariant::fromValue(PortalShortcuts{shortcut}),
                 QString(),
                 QVariantMap()},
                RequestKind::Bind,
                registrationTimeoutMs);
}

void PortalGlobalShortcutBinder::sendRequest(const QString &method,
                                             const QVariantList &arguments,
                                             RequestKind kind,
                                             int timeoutMs)
{
    auto *request = new PortalRequest(this);
    m_request = request;
    connect(request, &PortalRequest::answered, this,
            [this, kind](uint response, const QVariantMap &results) {
        processRequestResponse(kind, response, results);
    });
    connect(request, &PortalRequest::failed, this, [this, kind](const QString &error) {
        requestFailed(kind, error);
    });
    connect(request, &PortalRequest::timedOut, this, [this, kind] {
        requestFailed(kind,
                      kind == RequestKind::Bind
                          ? QStringLiteral("Your desktop didn't finish setting the shortcut.")
                          : QStringLiteral("Your desktop didn't answer. Try again."));
    });
    request->send(QString::fromLatin1(shortcutInterface), method, arguments, timeoutMs);
}

void PortalGlobalShortcutBinder::processRequestResponse(RequestKind kind,
                                                        uint response,
                                                        const QVariantMap &results)
{
    if (response != 0) {
        requestFailed(kind,
                      response == 1
                          ? QStringLiteral("Setup was cancelled. Try again.")
                          : QStringLiteral("Couldn't set the shortcut. Try again."));
        return;
    }

    if (kind == RequestKind::CreateForRestore
        || kind == RequestKind::CreateForRegistration) {
        const QVariant session = results.value(QStringLiteral("session_handle"));
        QString sessionPath = qdbus_cast<QDBusObjectPath>(session).path();
        if (sessionPath.isEmpty()) {
            sessionPath = session.toString();
        }
        m_pendingSessionPath = QDBusObjectPath(sessionPath);
        if (m_pendingSessionPath.path().isEmpty()) {
            requestFailed(kind, QStringLiteral("The portal returned no Global Shortcut session"));
            return;
        }
        kind == RequestKind::CreateForRegistration ? bindShortcuts()
                                                    : listShortcuts();
        return;
    }
    if (kind == RequestKind::List) {
        QString trigger;
        if (shortcutTrigger(results, &trigger)) {
            activatePendingSession(trigger);
        } else {
            closePendingSession();
        }
        return;
    }
    if (kind == RequestKind::Bind) {
        QString trigger;
        if (!shortcutTrigger(results, &trigger)) {
            requestFailed(kind, QStringLiteral("Your desktop didn't say which keys it assigned."));
            return;
        }
        m_settings->setPortalShortcutCleared(action().role, false);
        activatePendingSession(trigger);
        emit registrationFinished(true, m_triggerDescription);
    }
}

// The timestamps are milliseconds on a clock of the desktop's choosing, so
// only their difference means anything: the physical hold, which a release
// handled late would otherwise stretch. A backend that sends no timestamp
// (zero) leaves the controller to time the hold.
void PortalGlobalShortcutBinder::handleActivated(const QDBusObjectPath &sessionHandle,
                                                  const QString &id,
                                                  qulonglong timestamp,
                                                  const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionPath.path()
        && id == action().id) {
        // A held key's auto-repeat can arrive as more activations; the hold
        // runs from the first.
        if (m_activatedAtMs == 0) {
            m_activatedAtMs = timestamp;
        }
        emit activated();
    }
}

void PortalGlobalShortcutBinder::handleDeactivated(const QDBusObjectPath &sessionHandle,
                                                    const QString &id,
                                                    qulonglong timestamp,
                                                    const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionPath.path()
        && id == action().id) {
        const qulonglong activatedAtMs = std::exchange(m_activatedAtMs, 0);
        emit deactivated(activatedAtMs != 0 && timestamp >= activatedAtMs ? qint64(timestamp - activatedAtMs)
                                                                         : -1);
    }
}

void PortalGlobalShortcutBinder::requestFailed(RequestKind kind, const QString &reason)
{
    const bool registration = kind == RequestKind::CreateForRegistration
        || kind == RequestKind::Bind;
    closePendingSession();
    if (registration) {
        emit registrationFinished(false, reason);
        createSession(false);
    }
}

bool PortalGlobalShortcutBinder::shortcutTrigger(const QVariantMap &results,
                                                 QString *trigger) const
{
    const PortalShortcuts shortcuts = qdbus_cast<PortalShortcuts>(
        results.value(QStringLiteral("shortcuts")));
    for (const PortalShortcut &shortcut : shortcuts) {
        if (shortcut.id == action().id) {
            *trigger = shortcut.properties.value(
                QStringLiteral("trigger_description")).toString();
            return !trigger->isEmpty();
        }
    }
    trigger->clear();
    return false;
}

void PortalGlobalShortcutBinder::activatePendingSession(const QString &trigger)
{
    const QDBusObjectPath previous = m_sessionPath;
    m_sessionPath = m_pendingSessionPath;
    m_pendingSessionPath = {};
    m_triggerDescription = trigger;
    if (!previous.path().isEmpty() && previous.path() != m_sessionPath.path()) {
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
            QString::fromLatin1(portalService),
            previous.path(),
            QString::fromLatin1(sessionInterface),
            QStringLiteral("Close")));
    }
    emit bindingChanged();
}

void PortalGlobalShortcutBinder::closePendingSession()
{
    if (m_pendingSessionPath.path().isEmpty()) {
        return;
    }
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        m_pendingSessionPath.path(),
        QString::fromLatin1(sessionInterface),
        QStringLiteral("Close")));
    m_pendingSessionPath = {};
}

void PortalGlobalShortcutBinder::cancelRequest()
{
    if (m_request) {
        m_request->cancel();
    }
}

} // namespace speecher
