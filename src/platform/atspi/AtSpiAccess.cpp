#include "platform/atspi/AtSpiAccess.h"

#include "platform/LinuxDesktopIntegration.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <optional>
#include <utility>

namespace speecher::atspi {

namespace {

constexpr auto accessibilityVariable = "QT_LINUX_ACCESSIBILITY_ALWAYS_ON";
// The whole of the environment.d file enableAccessibilityPermanently()
// writes; removal deletes the file only while it still says exactly this.
constexpr char accessibilityEnvironment[] =
    "# Enable AT-SPI for Speecher target-aware desktop integration.\n"
    "QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1\n";
constexpr int accessibilityCallTimeoutMs = 250;

QString accessibilityEnvironmentPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/environment.d/90-speecher-atspi.conf");
}

bool valueEnablesAccessibility(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("1")
        || normalized == QStringLiteral("true")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on");
}

bool persistentAccessibilityConfigured()
{
    if (qEnvironmentVariableIsSet(accessibilityVariable)) {
        return valueEnablesAccessibility(qEnvironmentVariable(accessibilityVariable));
    }

    QDir directory(QFileInfo(accessibilityEnvironmentPath()).absolutePath());
    const QString assignment = QString::fromLatin1(accessibilityVariable) + QLatin1Char('=');
    std::optional<bool> configured;
    for (const QString &name : directory.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name)) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.startsWith(assignment)) {
                configured = valueEnablesAccessibility(line.mid(assignment.size()));
            }
        }
    }
    return configured.value_or(false);
}

QDBusMessage callStatusProperties(const QString &method, const QVariantList &arguments)
{
    QDBusMessage request = QDBusMessage::createMethodCall(
        QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        method);
    request.setArguments(QVariantList{QStringLiteral("org.a11y.Status")} + arguments);
    return QDBusConnection::sessionBus().call(request, QDBus::Block, accessibilityCallTimeoutMs);
}

// Nothing when the bus launcher is unreachable or has no such property.
std::optional<bool> statusFlag(const QString &name)
{
    const QDBusMessage reply = callStatusProperties(QStringLiteral("Get"), {name});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return std::nullopt;
    }
    return reply.arguments().first().value<QDBusVariant>().variant().toBool();
}

QDBusMessage setStatusFlag(const QString &name, bool value)
{
    return callStatusProperties(QStringLiteral("Set"), {name, QVariant::fromValue(QDBusVariant(value))});
}

bool runtimeAccessibilityEnabled()
{
    return statusFlag(QStringLiteral("IsEnabled")).value_or(false);
}

// Chromium and Electron apps build their accessibility tree only for a
// screen reader, which they learn of from ScreenReaderEnabled; IsEnabled does
// not move them. The flag stands in for the screen-reader-enabled GSetting,
// which GNOME may start Orca from, hence mayAnnounceScreenReader(). That
// setting outlives the session, so Speecher sets it back on a clean quit, but
// only when it was the one to turn it on. at-spi2-core 2.61 dropped the flag;
// there the Get finds nothing and nothing is set.
void announceScreenReader()
{
    const QString flag = QStringLiteral("ScreenReaderEnabled");
    auto *app = QCoreApplication::instance();
    if (!app || !mayAnnounceScreenReader(qEnvironmentVariable("XDG_CURRENT_DESKTOP"))) {
        return;
    }
    // Missing, or already on: either way not Speecher's to change.
    const std::optional<bool> announced = statusFlag(flag);
    if (announced != false) {
        return;
    }
    const QDBusMessage reply = setStatusFlag(flag, true);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning().noquote() << "Could not announce a screen reader to Chromium apps:"
                             << reply.errorMessage();
        return;
    }
    QObject::connect(app, &QCoreApplication::aboutToQuit, app, [flag] { setStatusFlag(flag, false); });
}

} // namespace

AccessibilityState accessibilityState()
{
    return {
#ifdef SPEECHER_WITH_ATSPI
        true,
#else
        false,
#endif
        runtimeAccessibilityEnabled(),
        persistentAccessibilityConfigured(),
    };
}

bool requestAccessibility(QString *error)
{
    const QDBusMessage reply = setStatusFlag(QStringLiteral("IsEnabled"), true);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) {
            *error = reply.errorMessage().isEmpty()
                ? QStringLiteral("Could not enable desktop accessibility for this session")
                : reply.errorMessage();
        }
        return false;
    }
    announceScreenReader();
    return true;
}

bool mayAnnounceScreenReader(const QString &currentDesktop)
{
    // Untested on GNOME, where Shell may start Orca the moment the flag turns
    // on. XDG_CURRENT_DESKTOP is a colon-separated list, such as ubuntu:GNOME.
    const QStringList desktops = currentDesktop.split(QLatin1Char(':'));
    return !desktops.contains(QStringLiteral("GNOME"), Qt::CaseInsensitive);
}

bool enableAccessibilityPermanently(QString *error)
{
#ifndef SPEECHER_WITH_ATSPI
    if (error) {
        *error = QStringLiteral("This Speecher build does not include AT-SPI support");
    }
    return false;
#else
    const QString path = accessibilityEnvironmentPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) {
            *error = QStringLiteral("Could not create the user environment directory");
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Could not save the accessibility setting: %1")
                         .arg(file.errorString());
        }
        return false;
    }
    file.write(accessibilityEnvironment);
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not save the accessibility setting: %1")
                         .arg(file.errorString());
        }
        return false;
    }

    qputenv(accessibilityVariable, "1");
    if (!requestAccessibility(error)) {
        return false;
    }
    return true;
#endif
}

void removePermanentAccessibility(DesktopIntegrationRemoval &removal)
{
    const QString name = QStringLiteral("accessibility setting");
    const QString path = accessibilityEnvironmentPath();
    QFile file(path);
    if (!file.exists()) {
        removal.absent.append(name);
        return;
    }
    if (!file.open(QIODevice::ReadOnly) || file.readAll() != accessibilityEnvironment) {
        removal.failed.append(
            QStringLiteral("%1: %2 holds settings other than Speecher's").arg(name, path));
        return;
    }
    file.close();
    if (file.remove()) {
        removal.removed.append(name);
    } else {
        removal.failed.append(QStringLiteral("%1: could not delete %2").arg(name, path));
    }
}

#ifdef SPEECHER_WITH_ATSPI
AccessibleHandle::AccessibleHandle(AtspiAccessible *accessible)
    : m_accessible(accessible)
{
}

AtspiAccessible *AccessibleHandle::get() const
{
    return ATSPI_ACCESSIBLE(m_accessible);
}
#endif

AccessibleHandle::~AccessibleHandle()
{
    reset();
}

AccessibleHandle::AccessibleHandle(AccessibleHandle &&other) noexcept
    : m_accessible(std::exchange(other.m_accessible, nullptr))
{
}

AccessibleHandle &AccessibleHandle::operator=(AccessibleHandle &&other) noexcept
{
    if (this != &other) {
        reset();
        m_accessible = std::exchange(other.m_accessible, nullptr);
    }
    return *this;
}

AccessibleHandle::operator bool() const
{
    return m_accessible != nullptr;
}

void AccessibleHandle::reset()
{
#ifdef SPEECHER_WITH_ATSPI
    if (m_accessible) {
        g_object_unref(ATSPI_ACCESSIBLE(m_accessible));
    }
#endif
    m_accessible = nullptr;
}

#ifdef SPEECHER_WITH_ATSPI
void clearError(GError **error)
{
    if (error && *error) {
        g_error_free(*error);
        *error = nullptr;
    }
}

QString takeString(gchar *value)
{
    const QString result = QString::fromUtf8(value ? value : "");
    g_free(value);
    return result;
}

bool hasState(AtspiAccessible *object, AtspiStateType state)
{
    AtspiStateSet *states = object ? atspi_accessible_get_state_set(object) : nullptr;
    const bool present = states && atspi_state_set_contains(states, state);
    if (states) {
        g_object_unref(states);
    }
    return present;
}
#endif

} // namespace speecher::atspi
