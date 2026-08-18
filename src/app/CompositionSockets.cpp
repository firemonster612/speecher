#include "app/CompositionSockets.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>

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
    const QString base = QStringLiteral("speecher-%1").arg(userToken());
    return suffix.isEmpty() ? base : base + QLatin1Char('-') + suffix;
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
