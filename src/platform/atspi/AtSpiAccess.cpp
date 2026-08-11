#include "platform/atspi/AtSpiAccess.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>
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

bool runtimeAccessibilityEnabled()
{
    QDBusInterface properties(
        QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QDBusConnection::sessionBus());
    properties.setTimeout(2000);
    const QDBusMessage reply = properties.call(
        QStringLiteral("Get"),
        QStringLiteral("org.a11y.Status"),
        QStringLiteral("IsEnabled"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return false;
    }
    return reply.arguments().first().value<QDBusVariant>().variant().toBool();
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
    QDBusInterface properties(
        QStringLiteral("org.a11y.Bus"),
        QStringLiteral("/org/a11y/bus"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QDBusConnection::sessionBus());
    properties.setTimeout(2000);
    const QDBusMessage reply = properties.call(
        QStringLiteral("Set"),
        QStringLiteral("org.a11y.Status"),
        QStringLiteral("IsEnabled"),
        QVariant::fromValue(QDBusVariant(true)));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error) {
            *error = reply.errorMessage().isEmpty()
                ? QStringLiteral("Could not enable desktop accessibility for this session")
                : reply.errorMessage();
        }
        return false;
    }
    return true;
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
    file.write("# Enable AT-SPI for Speecher target-aware desktop integration.\n"
               "QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1\n");
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
