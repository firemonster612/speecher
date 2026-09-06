#include "providers/ClaudeCredentialStorage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace speecher {

ClaudeCredentialStorage::ClaudeCredentialStorage(const QString &path)
    : m_path(path)
{
#ifdef Q_OS_MACOS
    const QString defaultPath = QDir::homePath() + QStringLiteral("/.claude/.credentials.json");
    if (QFileInfo(path).absoluteFilePath() != defaultPath) return;

    // Claude Code hashes the raw config directory string, only when nonempty.
    const QByteArray configDir = qgetenv("CLAUDE_CONFIG_DIR");
    m_service = "Claude Code-credentials";
    if (!configDir.isEmpty()) {
        m_service += '-' + QCryptographicHash::hash(configDir, QCryptographicHash::Sha256).toHex().left(8);
        m_path = QDir(QString::fromUtf8(configDir)).filePath(QStringLiteral(".credentials.json"));
    }
    m_account = qgetenv("USER");
    if (m_account.isEmpty()) {
        if (const passwd *user = getpwuid(getuid())) m_account = user->pw_name;
        if (m_account.isEmpty()) m_account = "claude-code-user";
    }
    SecKeychainItemRef item = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(nullptr, m_service.size(), m_service.constData(),
                                                          m_account.size(), m_account.constData(),
                                                          nullptr, nullptr, &item);
    if (item) CFRelease(item);
    // Match Claude's plaintext fallback only when no Keychain item exists.
    // Permission failures must not silently select some other login.
    if (status == errSecItemNotFound && QFileInfo::exists(m_path)) {
        m_service.clear();
        m_account.clear();
    }
#endif
}

QByteArray ClaudeCredentialStorage::read(QString *error) const
{
#ifdef Q_OS_MACOS
    if (!m_service.isEmpty()) {
        UInt32 length = 0;
        void *data = nullptr;
        const OSStatus status = SecKeychainFindGenericPassword(nullptr, m_service.size(), m_service.constData(),
                                                              m_account.size(), m_account.constData(),
                                                              &length, &data, nullptr);
        if (status != errSecSuccess) {
            *error = QStringLiteral("Could not read Claude login from macOS Keychain (%1); run claude and use /login").arg(status);
            return {};
        }
        const QByteArray bytes(static_cast<const char *>(data), length);
        SecKeychainItemFreeContent(nullptr, data);
        return bytes;
    }
#endif
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Claude credentials not found at %1; run claude in a terminal and use the /login command").arg(m_path);
        return {};
    }
    return file.readAll();
}

bool ClaudeCredentialStorage::write(const QByteArray &bytes, QString *error) const
{
#ifdef Q_OS_MACOS
    if (!m_service.isEmpty()) {
        SecKeychainItemRef item = nullptr;
        OSStatus status = SecKeychainFindGenericPassword(nullptr, m_service.size(), m_service.constData(),
                                                        m_account.size(), m_account.constData(),
                                                        nullptr, nullptr, &item);
        if (status == errSecSuccess) {
            status = SecKeychainItemModifyAttributesAndData(item, nullptr, bytes.size(), bytes.constData());
            CFRelease(item);
        }
        if (status != errSecSuccess) {
            *error = QStringLiteral("Could not save refreshed Claude login to macOS Keychain (%1)").arg(status);
            return false;
        }
        return true;
    }
#endif
    QSaveFile destination(m_path);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Could not safely save refreshed Claude credentials");
        return false;
    }
    destination.setPermissions(QFileInfo(m_path).permissions());
    if (destination.write(bytes) != bytes.size() || !destination.commit()) {
        *error = QStringLiteral("Could not safely save refreshed Claude credentials");
        return false;
    }
    return true;
}

QString ClaudeCredentialStorage::lockPath() const
{
    if (m_service.isEmpty()) return m_path + QStringLiteral(".lock");
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(directory);
    const QByteArray identity = m_service + '\0' + m_account;
    return QDir(directory).filePath(QStringLiteral("claude-keychain-%1.lock").arg(
        QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex())));
}

} // namespace speecher
