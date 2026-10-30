#include "app/LogFile.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>

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

} // namespace

LogFile::LogFile(const QString &path, qint64 maxBytes)
    : m_file(path)
    , m_maxBytes(maxBytes)
{
    // A log that cannot be opened stays closed, and write() drops messages.
    (void)m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void LogFile::write(QtMsgType type, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    rotateIfFull();
    if (!m_file.isOpen()) {
        return;
    }
    QTextStream stream(&m_file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << ' ' << levelName(type) << ' ' << message << '\n';
    stream.flush();
}

void LogFile::rotateIfFull()
{
    if (!m_file.isOpen() || m_file.size() < m_maxBytes) {
        return;
    }
    const QString path = m_file.fileName();
    const QString previous = path + QStringLiteral(".1");
    m_file.close();
    QFile::remove(previous);
    // Windows refuses to rename a log another Speecher process has open; the
    // log starts over in place then, which still keeps it to its limit.
    const bool moved = QFile::rename(path, previous);
    (void)m_file.open(QIODevice::WriteOnly | QIODevice::Text | (moved ? QIODevice::Append : QIODevice::Truncate));
}

} // namespace speecher
