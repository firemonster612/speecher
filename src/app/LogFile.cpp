#include "app/LogFile.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace speecher {

namespace {

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    case QtInfoMsg:
        break;
    }
    return "info";
}

// Opens file on path so that every write lands at its end, wherever another
// Speecher process has cut it to. Qt's Append does that on POSIX, through
// O_APPEND, but on Windows only moves to the end once, at open; a handle with
// append access alone always writes at the end.
bool openForAppend(QFile &file, const QString &path)
{
#ifdef Q_OS_WIN
    const HANDLE handle =
        CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()),
                    FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_WRONLY | _O_BINARY);
    if (descriptor == -1) {
        CloseHandle(handle);
        return false;
    }
    if (!file.open(descriptor, QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text,
                   QFileDevice::AutoCloseHandle)) {
        _close(descriptor);
        return false;
    }
    return true;
#else
    file.setFileName(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
#endif
}

} // namespace

LogFile::LogFile(const QString &path, qint64 maxBytes)
    : m_path(path)
    , m_maxBytes(maxBytes)
{
    // A log that cannot be opened stays closed, and write() drops messages.
    openForAppend(m_file, m_path);
}

void LogFile::write(QtMsgType type, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen()) {
        return;
    }
    cutIfFull();
    QTextStream stream(&m_file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << ' ' << levelName(type) << ' ' << message << '\n';
    stream.flush();
}

// The log is cut in place rather than renamed: every Speecher process keeps
// it open, and a renamed or deleted log would keep taking their lines, and
// its disk space, out of sight. A line another process writes between the
// copy and the cut is lost.
void LogFile::cutIfFull()
{
    if (m_file.size() < m_maxBytes) {
        return;
    }
    const QString previous = m_path + QStringLiteral(".1");
    QFile::remove(previous);
    // Best effort: Windows can refuse to copy a file another process is
    // writing, and the log is cut all the same.
    (void)QFile::copy(m_path, previous);
    QFile cut(m_path);
    (void)cut.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

} // namespace speecher
