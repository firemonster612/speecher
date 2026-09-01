#include "platform/PortalGlobalShortcutBinder.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
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
    if (parts.isEmpty()) {
        return {};
    }
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
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();

    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
            QStringLiteral("KDE"), Qt::CaseInsensitive)) {
        m_unsupportedReason = QStringLiteral("Plasma uses its native Global Shortcut service");
        return;
    }
    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"),
                                                     Qt::CaseInsensitive)) {
        m_unsupportedReason = QStringLiteral("The Global Shortcuts portal requires native Wayland");
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) {
        m_unsupportedReason = QStringLiteral("The session D-Bus is unavailable");
        return;
    }

    QDBusMessage introspect = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QStringLiteral("org.freedesktop.DBus.Introspectable"),
        QStringLiteral("Introspect"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(
        introspect, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage
        || reply.arguments().isEmpty()
        || !reply.arguments().first().toString().contains(
            QString::fromLatin1(shortcutInterface))) {
        m_unsupportedReason = QStringLiteral("The desktop does not provide the Global Shortcuts portal");
        return;
    }
    m_supported = true;

    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(shortcutInterface),
        QStringLiteral("Activated"),
        this,
        SLOT(handleActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
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

QString PortalGlobalShortcutBinder::unsupportedReason() const
{
    return m_supported ? QString() : m_unsupportedReason;
}

void PortalGlobalShortcutBinder::bind()
{
    if (m_supported) {
        createSession(false);
    }
}

void PortalGlobalShortcutBinder::registerShortcut()
{
    if (!m_supported) {
        emit registrationFinished(false, m_unsupportedReason);
        return;
    }
    createSession(true);
}

QKeySequence PortalGlobalShortcutBinder::shortcut() const
{
    QString portable = m_triggerDescription;
    portable.replace(QStringLiteral("Super"), QStringLiteral("Meta"),
                     Qt::CaseInsensitive);
    return QKeySequence::fromString(portable, QKeySequence::NativeText);
}

QString PortalGlobalShortcutBinder::shortcutDisplay() const
{
    return m_triggerDescription;
}

bool PortalGlobalShortcutBinder::setShortcut(const QKeySequence &, QString *error)
{
    if (error) {
        *error = QStringLiteral("The desktop chooses portal Global Shortcuts in its own dialog");
    }
    return false;
}

bool PortalGlobalShortcutBinder::ensureHostIdentity()
{
    if (m_identityReady) {
        return true;
    }
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")
        || QFileInfo::exists(QStringLiteral("/.flatpak-info"))) {
        m_identityReady = true;
        return true;
    }

    QDBusMessage registration = QDBusMessage::createMethodCall(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(registryInterface),
        QStringLiteral("Register"));
    registration.setArguments({QString::fromLatin1(appId), QVariantMap()});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(
        registration, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError error(reply);
        if (!isUnknownRegistryCall(error)) {
            m_unsupportedReason = QStringLiteral("Could not register Speecher with the portal: %1")
                                      .arg(error.message());
            return false;
        }
    }
    m_identityReady = true;
    return true;
}

void PortalGlobalShortcutBinder::createSession(bool registration)
{
    ++m_requestGeneration;
    if (!m_requestPath.path().isEmpty()) {
        QDBusInterface request(QString::fromLatin1(portalService),
                               m_requestPath.path(),
                               QString::fromLatin1(requestInterface),
                               QDBusConnection::sessionBus());
        request.asyncCall(QStringLiteral("Close"));
    }
    disconnectRequest();
    closeSession();
    m_triggerDescription.clear();

    if (!ensureHostIdentity()) {
        if (registration) {
            emit registrationFinished(false, m_unsupportedReason);
        }
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
                {QVariant::fromValue(m_sessionPath), QVariantMap()},
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
                {QVariant::fromValue(m_sessionPath),
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
    const quint64 generation = ++m_requestGeneration;
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

    QDBusInterface portal(QString::fromLatin1(portalService),
                          QString::fromLatin1(portalPath),
                          QString::fromLatin1(shortcutInterface),
                          QDBusConnection::sessionBus());
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCallWithArgumentList(member, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_requestGeneration) {
            return;
        }
        if (reply.isError()) {
            requestFailed(reply.error().message());
            return;
        }
        if (reply.value().path() == m_requestPath.path()) {
            return;
        }
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

    QTimer::singleShot(timeoutMs, this, [this, generation, kind] {
        if (generation != m_requestGeneration) {
            return;
        }
        requestFailed(
            kind == RequestKind::Bind
                ? QStringLiteral("The desktop did not finish Global Shortcut registration")
                : QStringLiteral("The Global Shortcuts portal did not respond"),
            kind == RequestKind::CreateForRestore
                || kind == RequestKind::CreateForRegistration);
    });
}

void PortalGlobalShortcutBinder::handleRequestResponse(uint response,
                                                       const QVariantMap &results)
{
    if (message().path() != m_requestPath.path()) {
        return;
    }
    const RequestKind kind = m_requestKind;
    disconnectRequest();
    ++m_requestGeneration;
    if (response != 0) {
        m_requestKind = kind;
        requestFailed(response == 1
                          ? QStringLiteral("Global Shortcut registration was cancelled")
                          : QStringLiteral("Global Shortcut registration failed"));
        return;
    }

    if (kind == RequestKind::CreateForRestore
        || kind == RequestKind::CreateForRegistration) {
        m_sessionPath = QDBusObjectPath(results.value(
            QStringLiteral("session_handle")).toString());
        if (m_sessionPath.path().isEmpty()) {
            m_requestKind = kind;
            requestFailed(QStringLiteral("The portal returned no Global Shortcut session"));
            return;
        }
        kind == RequestKind::CreateForRegistration ? bindShortcuts()
                                                    : listShortcuts();
        return;
    }
    if (kind == RequestKind::List) {
        applyShortcuts(results);
        if (m_triggerDescription.isEmpty()) {
            closeSession();
        }
        return;
    }
    if (kind == RequestKind::Bind) {
        applyShortcuts(results);
        if (m_triggerDescription.isEmpty()) {
            m_triggerDescription = GlobalShortcutBinder::defaultShortcut().toString(
                QKeySequence::NativeText);
        }
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

void PortalGlobalShortcutBinder::handleDeactivated(const QDBusObjectPath &sessionHandle,
                                                    const QString &id,
                                                    qulonglong,
                                                    const QVariantMap &)
{
    if (sessionHandle.path() == m_sessionPath.path()
        && id == QString::fromLatin1(shortcutId)) {
        emit deactivated();
    }
}

void PortalGlobalShortcutBinder::requestFailed(const QString &reason, bool unsupported)
{
    const bool registration = m_requestKind == RequestKind::CreateForRegistration
        || m_requestKind == RequestKind::Bind;
    disconnectRequest();
    ++m_requestGeneration;
    closeSession();
    if (unsupported) {
        m_supported = false;
        m_unsupportedReason = reason;
    }
    if (registration) {
        emit registrationFinished(false, reason);
    }
}

void PortalGlobalShortcutBinder::applyShortcuts(const QVariantMap &results)
{
    const PortalShortcuts shortcuts = qdbus_cast<PortalShortcuts>(
        results.value(QStringLiteral("shortcuts")));
    for (const PortalShortcut &shortcut : shortcuts) {
        if (shortcut.id == QString::fromLatin1(shortcutId)) {
            m_triggerDescription = shortcut.properties.value(
                QStringLiteral("trigger_description")).toString();
            return;
        }
    }
    m_triggerDescription.clear();
}

void PortalGlobalShortcutBinder::closeSession()
{
    if (m_sessionPath.path().isEmpty()) {
        return;
    }
    QDBusInterface session(QString::fromLatin1(portalService),
                           m_sessionPath.path(),
                           QString::fromLatin1(sessionInterface),
                           QDBusConnection::sessionBus());
    session.asyncCall(QStringLiteral("Close"));
    m_sessionPath = {};
}

void PortalGlobalShortcutBinder::disconnectRequest()
{
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
