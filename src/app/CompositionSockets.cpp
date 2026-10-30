#include "app/CompositionSockets.h"

#include "core/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace speecher {
namespace {

QString userToken()
{
#ifdef Q_OS_UNIX
    return QString::number(getuid());
#else
    const QString user = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("user")));
    return QString::fromLatin1(QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
#endif
}

} // namespace

QString appSocketName(const QString &suffix)
{
    const QString base = SettingsKeys::application() + QLatin1Char('-') + userToken();
    const QString name = suffix.isEmpty() ? base : base + QLatin1Char('-') + suffix;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // A bare name would be a socket in the shared /tmp, where another user
    // can take it first or, with a loose umask, connect to it. The runtime
    // directory is this user's alone.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)).filePath(name);
#else
    return name;
#endif
}

QString executablePathSocketName()
{
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    QString path = executable.canonicalFilePath();
    if (path.isEmpty()) {
        path = executable.absoluteFilePath();
    }
    const QByteArray digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return appSocketName(QString::fromLatin1(digest));
}

} // namespace speecher
