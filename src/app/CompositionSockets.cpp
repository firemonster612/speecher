#include "app/CompositionSockets.h"

#include "core/OwnerOnly.h"
#include "core/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/socket.h>
#endif
#ifdef Q_OS_WIN
#include <QScopeGuard>
#include <qt_windows.h>
#include <sddl.h>
#endif

namespace speecher {
namespace {

QString shortDigest(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

#ifdef Q_OS_WIN
QString userNameToken()
{
    return shortDigest(qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("user"))));
}

// This process's account SID as text, such as "S-1-5-21-...". Empty if
// Windows will not say.
QString accountSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    const auto closeToken = qScopeGuard([token] { CloseHandle(token); });
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray user(int(size), Qt::Uninitialized);
    if (size == 0 || !GetTokenInformation(token, TokenUser, user.data(), size, &size)) {
        return {};
    }
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &text)) {
        return {};
    }
    const QString sid = QString::fromWCharArray(text);
    LocalFree(text);
    return sid;
}
#endif

QString userToken()
{
#ifdef Q_OS_UNIX
    return QString::number(getuid());
#else
    // The SID, not the user name: MACHINE\alice and DOMAIN\alice share a
    // name, and with it would share the pipe and the instance mutex.
    const QString sid = accountSid();
    return sid.isEmpty() ? userNameToken() : shortDigest(sid);
#endif
}

#ifdef Q_OS_UNIX
// Whether path is a folder of this user's that no one else may enter.
bool privateFolder(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).isDir() && isOwnerOnly(path);
}
#endif

} // namespace

QString appSocketName(const QString &suffix)
{
    const QString base = SettingsKeys::application() + QLatin1Char('-') + userToken();
    return suffix.isEmpty() ? base : base + QLatin1Char('-') + suffix;
}

QString executablePathSocketName()
{
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    QString path = executable.canonicalFilePath();
    if (path.isEmpty()) {
        path = executable.absoluteFilePath();
    }
    return appSocketName(shortDigest(path));
}

#ifdef Q_OS_WIN
QString legacyAppSocketName()
{
    return SettingsKeys::application() + QLatin1Char('-') + userNameToken();
}
#endif

#ifdef Q_OS_UNIX
QString inRuntimeDirectory(const QString &name)
{
    const QString session = QStringLiteral("/run/user/%1").arg(getuid());
    const QString directory = qEnvironmentVariableIsEmpty("XDG_RUNTIME_DIR") && privateFolder(session)
        ? session
        : QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return privateFolder(directory) ? QDir(directory).filePath(name) : QString();
}
#endif

bool peerIsThisUser(const QLocalSocket &socket)
{
#if defined(Q_OS_LINUX)
    ucred peer{};
    socklen_t size = sizeof(peer);
    return getsockopt(int(socket.socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0
        && peer.uid == getuid();
#elif defined(Q_OS_UNIX)
    uid_t uid = 0;
    gid_t gid = 0;
    return getpeereid(int(socket.socketDescriptor()), &uid, &gid) == 0 && uid == getuid();
#else
    Q_UNUSED(socket);
    return true;
#endif
}

} // namespace speecher
