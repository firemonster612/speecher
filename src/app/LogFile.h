#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QtGlobal>

namespace speecher {

// speecher.log: a timestamped line for each message of a Speecher process.
// It is kept to two files: once the log passes its size limit, what it holds
// is copied to path.1, replacing the older copy, and the log is emptied.
class LogFile {
public:
    LogFile(const QString &path, qint64 maxBytes);

    // Safe from any thread. Does nothing when the log could not be opened.
    void write(QtMsgType type, const QString &message);

private:
    void cutIfFull();

    QMutex m_mutex;
    // Kept apart from m_file, which forgets its name once opened from a
    // Windows handle.
    const QString m_path;
    QFile m_file;
    const qint64 m_maxBytes;
};

} // namespace speecher
