#include "providers/CodexCredentialStorage.h"
#include "providers/NativeCredentialStorage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <filesystem>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <cstdlib>
#endif

namespace speecher {

CodexCredentialStorage::CodexCredentialStorage()
{
    // Even an empty explicit override must never reach a real credential store.
    if (qEnvironmentVariableIsSet("SPEECHER_TEST_CODEX_AUTH_PATH")) {
        m_path = qEnvironmentVariable("SPEECHER_TEST_CODEX_AUTH_PATH");
        return;
    }
    QString home = qEnvironmentVariable("CODEX_HOME");
    if (home.isEmpty()) home = QDir::homePath() + QStringLiteral("/.codex");
    m_path = QDir(home).filePath(QStringLiteral("auth.json"));
    // Only a confirmed missing file permits fallback. Inaccessible paths and
    // dangling symlinks stay selected so their read error is reported.
    std::error_code error;
    const auto status = std::filesystem::symlink_status(QFileInfo(m_path).filesystemFilePath(), error);
    if (status.type() != std::filesystem::file_type::not_found
        || (error && error != std::errc::no_such_file_or_directory)) return;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QByteArray canonicalBytes;
#ifdef Q_OS_WIN
    QString canonical;
    // Rust std::fs::canonicalize returns the verbatim Windows path, including
    // the \\?\ prefix. Qt's canonicalFilePath strips it and hashes differently.
    const QString nativeHome = QDir::toNativeSeparators(home);
    const HANDLE directory = CreateFileW(reinterpret_cast<LPCWSTR>(nativeHome.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory != INVALID_HANDLE_VALUE) {
        const DWORD size = GetFinalPathNameByHandleW(directory, nullptr, 0, FILE_NAME_NORMALIZED);
        if (size > 0) {
            std::wstring path(size, L'\0');
            const DWORD length = GetFinalPathNameByHandleW(directory, path.data(), size, FILE_NAME_NORMALIZED);
            if (length > 0 && length < size) canonical = QString::fromWCharArray(path.data(), length);
        }
        CloseHandle(directory);
    }
    if (canonical.isEmpty()) canonical = home;
    canonicalBytes = canonical.toUtf8();
#else
    // Qt's canonicalFilePath normalizes Darwin paths to NFC. Rust hashes
    // realpath's native spelling, so retain raw environment and path bytes.
    QByteArray nativeHome = qgetenv("CODEX_HOME");
    if (nativeHome.isEmpty()) {
        nativeHome = qgetenv("HOME");
        if (nativeHome.isEmpty()) nativeHome = QFile::encodeName(QDir::homePath());
        nativeHome += nativeHome.endsWith('/') ? ".codex" : "/.codex";
    }
    char *resolved = realpath(nativeHome.constData(), nullptr);
    canonicalBytes = resolved ? QByteArray(resolved) : nativeHome;
    free(resolved);
    // Match Rust to_string_lossy without applying Unicode normalization.
    canonicalBytes = QString::fromUtf8(canonicalBytes).toUtf8();
#endif
    m_account = "cli|" + QCryptographicHash::hash(canonicalBytes, QCryptographicHash::Sha256).toHex().left(16);
#endif
}

QByteArray CodexCredentialStorage::read(QString *error) const
{
    if (!m_account.isEmpty()) return readNativeCredential("Codex Auth", m_account, error);
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not read Codex auth file; sign in with codex login");
        return {};
    }
    return file.readAll();
}

bool CodexCredentialStorage::canWrite(const QByteArray &bytes, QString *error) const
{
#ifdef Q_OS_MACOS
    if (!m_account.isEmpty()) {
        *error = QStringLiteral("Refresh the macOS Codex Keychain login with codex login; Speecher only reads this entry");
        return false;
    }
#endif
    return m_account.isEmpty() || canWriteNativeCredential("Codex Auth", m_account, bytes, error);
}

bool CodexCredentialStorage::write(const QByteArray &contents, QString *error) const
{
    if (!canWrite(contents, error)) return false;
    if (!m_account.isEmpty()) return writeNativeCredential("Codex Auth", m_account, contents, error);
    QString saveError;
#ifdef Q_OS_WIN
    QString temporaryPath;
    bool savedOk = false;
    {
        QTemporaryFile saved(QFileInfo(m_path).dir().filePath(
            QStringLiteral("auth.json.speecher.XXXXXX.tmp")));
        savedOk = saved.open() && saved.write(contents) == contents.size() && saved.flush();
        temporaryPath = saved.fileName();
        saveError = saved.errorString();
        if (savedOk) {
            saved.setAutoRemove(false);
        }
    }
    // QSaveFile cannot replace auth.json while Codex briefly has it open.
    // MoveFileEx keeps the crash-safe same-directory replacement and permits a retry.
    DWORD moveError = ERROR_SUCCESS;
    if (savedOk) {
        savedOk = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (MoveFileExW(reinterpret_cast<const wchar_t *>(temporaryPath.utf16()),
                            reinterpret_cast<const wchar_t *>(m_path.utf16()),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                savedOk = true;
                break;
            }
            moveError = GetLastError();
            if (attempt < 2) {
                Sleep(50);
            }
        }
    }
    if (savedOk) {
        saveError.clear();
    } else if (moveError != ERROR_SUCCESS) {
        saveError = QStringLiteral("Windows error %1").arg(moveError);
        QFile::remove(temporaryPath);
    }
#else
    QSaveFile saved(m_path);
    const bool savedOk = saved.open(QIODevice::WriteOnly)
        && saved.write(contents) == contents.size() && saved.commit();
    saveError = saved.errorString();
#endif
    if (!savedOk) *error = QStringLiteral("Could not write refreshed Codex auth file: %1").arg(saveError);
    return savedOk;
}

QString CodexCredentialStorage::lockPath() const
{
    if (m_account.isEmpty()) return m_path + QStringLiteral(".lock");
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("codex-keyring-%1.lock").arg(QString::fromUtf8(m_account.mid(4))));
}

} // namespace speecher
