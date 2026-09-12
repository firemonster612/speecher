#include "providers/NativeCredentialStorage.h"

#ifdef Q_OS_MACOS
#include <QProcess>
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <QStringDecoder>
#include <windows.h>
#include <wincred.h>
#endif

namespace speecher {

#ifdef Q_OS_MACOS
static QByteArray keychainCommand(const QByteArray &operation, const QByteArray &service,
                                 const QByteArray &account, const QByteArray &options, QString *error)
{
    for (const QByteArray &identity : {service, account}) {
        if (identity.contains('\0') || identity.contains('\r') || identity.contains('\n')) {
            *error = QStringLiteral("macOS Keychain login name contains unsupported control characters");
            return {};
        }
    }
    const auto quoted = [](QByteArray value) {
        value.replace("\\", "\\\\");
        value.replace("\"", "\\\"");
        return '\"' + value + '\"';
    };
    const QByteArray command = operation + " -s " + quoted(service)
        + " -a " + quoted(account) + ' ' + options + '\n';
    // security's interactive reader has a 4096-byte buffer, including its NUL.
    if (command.size() >= 4096) {
        const QString cli = service == "Codex Auth" ? QStringLiteral("codex login")
            : QStringLiteral("claude and use /login");
        *error = QStringLiteral("macOS Keychain login is too large for automatic refresh; run %1").arg(cli);
        return {};
    }
    return command;
}

static bool finishKeychainTool(QProcess &process, QString *error)
{
    if (!process.waitForStarted(1000) || !process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        *error = QStringLiteral("macOS Keychain tool did not finish; check Keychain access");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        // Neither stdout nor stderr is safe to include in errors.
        *error = QStringLiteral("macOS Keychain operation failed (exit %1); check Keychain access or sign in again")
                     .arg(process.exitCode());
        return false;
    }
    return true;
}
#endif

QByteArray readNativeCredential(const QByteArray &service, const QByteArray &account, QString *error)
{
#ifdef Q_OS_MACOS
    // QProcess normalizes argv to NFD on macOS. Stdin preserves the exact
    // service/account bytes, and security retains its partition across builds.
    const QByteArray command = keychainCommand("find-generic-password", service, account, "-w", error);
    if (command.isEmpty()) return {};
    QProcess process;
    process.start(QStringLiteral("/usr/bin/security"), {QStringLiteral("-i")});
    process.write(command);
    process.closeWriteChannel();
    if (!finishKeychainTool(process, error)) return {};
    QByteArray bytes = process.readAllStandardOutput();
    if (bytes.endsWith('\n')) bytes.chop(1);
    // security prints non-printable bytes as hex. A JSON object cannot itself
    // consist solely of hex digits, so decoding is unambiguous here.
    const QByteArray decoded = QByteArray::fromHex(bytes);
    if (!bytes.isEmpty() && decoded.toHex() == bytes.toLower()) bytes = decoded;
    return bytes;
#elif defined(Q_OS_WIN)
    const QString target = QString::fromUtf8(account + '.' + service);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        *error = QStringLiteral("Could not read Windows Credential Manager login (%1); sign in with codex login")
                     .arg(GetLastError());
        return {};
    }
    // Rust keyring's set_password stores UTF-16LE without a terminating NUL.
    QStringDecoder decoder(QStringDecoder::Utf16LE, QStringConverter::Flag::Stateless);
    const QString value = decoder(QByteArrayView(reinterpret_cast<const char *>(credential->CredentialBlob),
                                                credential->CredentialBlobSize));
    const bool invalid = credential->CredentialBlobSize % 2 != 0 || decoder.hasError();
    CredFree(credential);
    if (invalid) {
        *error = QStringLiteral("Windows Credential Manager login contains invalid UTF-16");
        return {};
    }
    return value.toUtf8();
#else
    Q_UNUSED(service);
    Q_UNUSED(account);
    *error = QStringLiteral("Native credential storage is unavailable on this platform");
    return {};
#endif
}

bool canWriteNativeCredential(const QByteArray &service, const QByteArray &account,
                              const QByteArray &bytes, QString *error)
{
#ifdef Q_OS_MACOS
    return !keychainCommand("add-generic-password -U", service, account, "-X " + bytes.toHex(), error).isEmpty();
#elif defined(Q_OS_WIN)
    Q_UNUSED(service);
    Q_UNUSED(account);
    if (QString::fromUtf8(bytes).size() * sizeof(ushort) > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        *error = QStringLiteral("Windows Credential Manager login is too large for automatic refresh; run codex login");
        return false;
    }
    return true;
#else
    Q_UNUSED(service);
    Q_UNUSED(account);
    Q_UNUSED(bytes);
    Q_UNUSED(error);
    return true;
#endif
}

bool writeNativeCredential(const QByteArray &service, const QByteArray &account,
                           const QByteArray &bytes, QString *error)
{
#ifdef Q_OS_MACOS
    const QByteArray command = keychainCommand("add-generic-password -U", service, account, "-X " + bytes.toHex(), error);
    if (command.isEmpty()) return false;
    SecKeychainItemRef item = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(nullptr, service.size(), service.constData(),
        account.size(), account.constData(), nullptr, nullptr, &item);
    if (item) CFRelease(item);
    if (status != errSecSuccess) {
        *error = QStringLiteral("Could not find macOS Keychain login for refresh (%1)").arg(status);
        return false;
    }
    // Only security carries apple-tool:. Native or osascript writes change
    // the partition and break the owning CLI's next read. Keep secrets on
    // stdin and send one command only, so EOF retains that command's status.
    QProcess process;
    process.start(QStringLiteral("/usr/bin/security"), {QStringLiteral("-i")});
    process.write(command);
    process.closeWriteChannel();
    return finishKeychainTool(process, error);
#elif defined(Q_OS_WIN)
    if (!canWriteNativeCredential(service, account, bytes, error)) return false;
    const QString target = QString::fromUtf8(account + '.' + service);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        *error = QStringLiteral("Could not find Windows Credential Manager login for refresh (%1)").arg(GetLastError());
        return false;
    }
    const QString value = QString::fromUtf8(bytes);
    // Windows is little-endian; retain the existing credential's metadata.
    credential->CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ushort *>(value.utf16()));
    credential->CredentialBlobSize = value.size() * sizeof(ushort);
    const bool saved = CredWriteW(credential, 0);
    const DWORD status = saved ? ERROR_SUCCESS : GetLastError();
    CredFree(credential);
    if (!saved) *error = QStringLiteral("Could not save refreshed Windows Credential Manager login (%1)").arg(status);
    return saved;
#else
    Q_UNUSED(service);
    Q_UNUSED(account);
    Q_UNUSED(bytes);
    *error = QStringLiteral("Native credential storage is unavailable on this platform");
    return false;
#endif
}

} // namespace speecher
