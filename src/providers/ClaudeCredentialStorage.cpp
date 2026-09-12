#include "providers/ClaudeCredentialStorage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_MACOS
#include <QProcess>
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
        // Self-signed releases have a per-build cdhash Keychain partition even
        // with a stable designated requirement. "Always Allow" therefore does
        // not survive updates. Use the same stable Apple tool as Claude Code;
        // never modify the shared item's ACL or pass credentials in arguments.
        QProcess process;
        process.start(QStringLiteral("/usr/bin/security"),
                      {QStringLiteral("find-generic-password"), QStringLiteral("-s"),
                       QString::fromUtf8(m_service), QStringLiteral("-a"),
                       QString::fromUtf8(m_account), QStringLiteral("-w")});
        process.closeWriteChannel();
        if (!process.waitForStarted(5000) || !process.waitForFinished(60000)) {
            process.kill();
            process.waitForFinished(1000);
            *error = QStringLiteral("Could not read Claude login from macOS Keychain: security tool did not finish");
            return {};
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            // Do not expose subprocess output, which may contain credentials.
            *error = QStringLiteral("Could not read Claude login from macOS Keychain (security exit %1); check Keychain access or run claude and use /login")
                         .arg(process.exitCode());
            return {};
        }
        QByteArray bytes = process.readAllStandardOutput();
        // security appends one newline; preserve whitespace in the stored data.
        if (bytes.endsWith('\n')) bytes.chop(1);
        // security prints a hex dump for non-printable bytes, including the
        // newlines in indented JSON. Credential documents are JSON objects, so
        // an entirely hexadecimal result cannot be a literal document.
        const QByteArray decoded = QByteArray::fromHex(bytes);
        if (!bytes.isEmpty() && decoded.toHex() == bytes.toLower()) bytes = decoded;
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
