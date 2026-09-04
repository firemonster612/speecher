#include "platform/PortalGlobalShortcutBinder.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <QUuid>

namespace speecher {
namespace {

constexpr auto portalService = "org.freedesktop.portal.Desktop";
constexpr auto portalPath = "/org/freedesktop/portal/desktop";
constexpr auto shortcutInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto requestInterface = "org.freedesktop.portal.Request";
constexpr auto registryInterface = "org.freedesktop.host.portal.Registry";
constexpr auto sessionInterface = "org.freedesktop.portal.Session";
constexpr auto appId = "io.github.firemonster612.speecher";
constexpr auto shortcutId = "toggle-dictation";
constexpr int createTimeoutMs = 5000;
constexpr int registrationTimeoutMs = 120000;

QDBusObjectPath predictedRequestPath(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService();
    sender.remove(0, sender.startsWith(QLatin1Char(':')) ? 1 : 0);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QDBusObjectPath(
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(sender, token));
}

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

PortalGlobalShortcutBinder::PortalGlobalShortcutBinder(QObject *parent)
    : GlobalShortcutBinder(parent)
    , m_requestTimer(new QTimer(this))
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();

    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"),
                                                     Qt::CaseInsensitive)) {
        m_unsupportedReason = QStringLiteral(
            "Automatic setup needs a Wayland session.");
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) {
        m_unsupportedReason = QStringLiteral(
            "Speecher can't reach your desktop settings.");
        return;
    }

    m_requestTimer->setSingleShot(true);
    connect(m_requestTimer, &QTimer::timeout, this, [this] {
        requestFailed(
            m_requestKind == RequestKind::Bind
                ? QStringLiteral("Your desktop didn't finish setting the shortcut.")
                : QStringLiteral("Your desktop didn't answer. Try again."),
            true);
    });

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
    // Press/release via the portal is a future cross-binder decision.
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

QKeySequence PortalGlobalShortcutBinder::shortcut() const
{
    return m_triggerDescription.isEmpty() ? QKeySequence()
                                          : GlobalShortcutBinder::defaultShortcut();
}

QString PortalGlobalShortcutBinder::shortcutDisplay() const
{
    return m_triggerDescription;
}

bool PortalGlobalShortcutBinder::setShortcut(const QKeySequence &, QString *error)
{
    if (error) {
        *error = QStringLiteral(
            "Your desktop picks this key combination itself. Use Choose shortcut instead.");
    }
    return false;
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
        m_registrationAfterIdentity = m_registrationAfterIdentity || registration;
        return false;
    }

    m_identityPending = true;
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
        createSession(requestedRegistration);
    });
    return false;
}

void PortalGlobalShortcutBinder::createSession(bool registration)
{
    if (!m_supported) {
        if (registration) {
            emit registrationFinished(false, m_unsupportedReason);
        }
        return;
    }
    if (!m_requestPath.path().isEmpty()) {
        closeRequest();
    }
    disconnectRequest();
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
                0,
                registration ? RequestKind::CreateForRegistration
                             : RequestKind::CreateForRestore,
                createTimeoutMs);
}

void PortalGlobalShortcutBinder::listShortcuts()
{
    sendRequest(QStringLiteral("ListShortcuts"),
                {QVariant::fromValue(m_pendingSessionPath), QVariantMap()},
                1,
                RequestKind::List,
                createTimeoutMs);
}

void PortalGlobalShortcutBinder::bindShortcuts()
{
    PortalShortcut shortcut;
    shortcut.id = QString::fromLatin1(shortcutId);
    shortcut.properties.insert(QStringLiteral("description"),
                               QStringLiteral("Toggle dictation"));
    shortcut.properties.insert(QStringLiteral("preferred_trigger"),
                               portalTrigger(GlobalShortcutBinder::defaultShortcut()));
    sendRequest(QStringLiteral("BindShortcuts"),
                {QVariant::fromValue(m_pendingSessionPath),
                 QVariant::fromValue(PortalShortcuts{shortcut}),
                 QString(),
                 QVariantMap()},
                3,
                RequestKind::Bind,
                registrationTimeoutMs);
}

void PortalGlobalShortcutBinder::sendRequest(const QString &member,
                                             QVariantList arguments,
                                             int optionsIndex,
                                             RequestKind kind,
                                             int timeoutMs)
{
    const QString token = QStringLiteral("speecher_%1")
                              .arg(QUuid::createUuid().toString(QUuid::Id128));
    QVariantMap options = arguments.at(optionsIndex).toMap();
    options.insert(QStringLiteral("handle_token"), token);
    arguments[optionsIndex] = options;

    m_requestKind = kind;
    m_requestPath = predictedRequestPath(token);
    const QDBusObjectPath predictedPath = m_requestPath;
    if (!QDBusConnection::sessionBus().connect(
            QString::fromLatin1(portalService),
            m_requestPath.path(),
            QString::fromLatin1(requestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(handleRequestResponse(uint,QVariantMap)))) {
        requestFailed(QStringLiteral("Could not watch the portal request"));
        return;
    }

    QDBusMessage portalCall = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(shortcutInterface),
        member);
    portalCall.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(portalCall), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, predictedPath, kind] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_requestPath.path() != predictedPath.path()
            || m_requestKind != kind) {
            return;
        }
        if (reply.isError()) {
            requestFailed(reply.error().message());
            return;
        }
        if (reply.value().path() == m_requestPath.path()) {
            return;
        }
        // xdg-desktop-portal < 0.9 really does return a different handle, so it is not dead code.
        QDBusConnection::sessionBus().disconnect(
            QString::fromLatin1(portalService),
            m_requestPath.path(),
            QString::fromLatin1(requestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(handleRequestResponse(uint,QVariantMap)));
        m_requestPath = reply.value();
        if (!QDBusConnection::sessionBus().connect(
                QString::fromLatin1(portalService),
                m_requestPath.path(),
                QString::fromLatin1(requestInterface),
                QStringLiteral("Response"),
                this,
                SLOT(handleRequestResponse(uint,QVariantMap)))) {
            requestFailed(QStringLiteral("Could not watch the portal request"));
        }
    });
    m_requestTimer->start(timeoutMs);
}

void PortalGlobalShortcutBinder::handleRequestResponse(uint response,
                                                       const QVariantMap &results)
{
    if (message().path() != m_requestPath.path()) {
        return;
    }
    const RequestKind kind = m_requestKind;
    if (response != 0) {
        requestFailed(response == 1
                          ? QStringLiteral("Setup was cancelled. Try again.")
                          : QStringLiteral("Couldn't set the shortcut. Try again."));
        return;
    }
    disconnectRequest();

    if (kind == RequestKind::CreateForRestore
        || kind == RequestKind::CreateForRegistration) {
        const QVariant session = results.value(QStringLiteral("session_handle"));
        QString sessionPath = qdbus_cast<QDBusObjectPath>(session).path();
        if (sessionPath.isEmpty()) {
            sessionPath = session.toString();
        }
        m_pendingSessionPath = QDBusObjectPath(sessionPath);
        if (m_pendingSessionPath.path().isEmpty()) {
            m_requestKind = kind;
            requestFailed(QStringLiteral("The portal returned no Global Shortcut session"));
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
            m_requestKind = kind;
            requestFailed(QStringLiteral("Your desktop didn't say which keys it assigned."));
            return;
        }
        activatePendingSession(trigger);
        emit registrationFinished(true, m_triggerDescription);
    }
}

void PortalGlobalShortcutBinder::handleActivated(const QDBusObjectPath &sessionHandle,
                                                  const QString &id,
                                                  qulonglong,
                                                  const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionPath.path()
        && id == QString::fromLatin1(shortcutId)) {
        emit activated();
    }
}

void PortalGlobalShortcutBinder::requestFailed(const QString &reason, bool closeOutstandingRequest)
{
    const RequestKind kind = m_requestKind;
    const bool registration = kind == RequestKind::CreateForRegistration
        || kind == RequestKind::Bind;
    if (closeOutstandingRequest) {
        closeRequest();
    }
    disconnectRequest();
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
        if (shortcut.id == QString::fromLatin1(shortcutId)) {
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

void PortalGlobalShortcutBinder::closeRequest()
{
    if (m_requestPath.path().isEmpty()) {
        return;
    }
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        m_requestPath.path(),
        QString::fromLatin1(requestInterface),
        QStringLiteral("Close")));
}

void PortalGlobalShortcutBinder::disconnectRequest()
{
    m_requestTimer->stop();
    if (!m_requestPath.path().isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QString::fromLatin1(portalService),
            m_requestPath.path(),
            QString::fromLatin1(requestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(handleRequestResponse(uint,QVariantMap)));
    }
    m_requestPath = {};
    m_requestKind = RequestKind::None;
}

} // namespace speecher
