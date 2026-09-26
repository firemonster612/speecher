#include "core/InsightsLog.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

namespace speecher {
namespace {

QByteArray jsonString(const QString &value)
{
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

// Written by hand because QJsonObject sorts its keys, and the line format,
// shared with the seed files, fixes their order.
QByteArray encode(const DictationRecord &record)
{
    return "{\"finishedAt\":" + jsonString(record.finishedAt.toString(Qt::ISODate))
        + ",\"audioMs\":" + QByteArray::number(record.audioMs)
        + ",\"words\":" + QByteArray::number(record.words)
        + ",\"app\":" + jsonString(record.appName)
        + ",\"profile\":" + jsonString(writingProfileName(record.profile)) + "}\n";
}

std::optional<DictationRecord> decode(const QByteArray &line)
{
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    const QDateTime finishedAt =
        QDateTime::fromString(object.value(QLatin1String("finishedAt")).toString(), Qt::ISODate);
    if (!finishedAt.isValid()) {
        return std::nullopt;
    }
    return DictationRecord{
        finishedAt,
        object.value(QLatin1String("audioMs")).toInt(),
        object.value(QLatin1String("words")).toInt(),
        object.value(QLatin1String("app")).toString(),
        writingProfileFromName(object.value(QLatin1String("profile")).toString()),
    };
}

} // namespace

InsightsLog::InsightsLog(const QString &path, Access access, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_access(access)
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    int lineNumber = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        ++lineNumber;
        if (line.isEmpty()) {
            continue;
        }
        if (const std::optional<DictationRecord> record = decode(line)) {
            m_records.append(*record);
        } else {
            qWarning().noquote() << "insights log skipped unreadable line" << lineNumber
                                 << "path=" + m_path;
        }
    }
}

const QList<DictationRecord> &InsightsLog::records() const
{
    return m_records;
}

void InsightsLog::append(const DictationRecord &record)
{
    m_records.append(record);
    if (m_access == Access::ReadWrite) {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        QFile file(m_path);
        QByteArray line = encode(record);
        if (file.open(QIODevice::ReadWrite | QIODevice::Append) && file.size() > 0) {
            // A line cut short by a crash would swallow this record on reload.
            file.seek(file.size() - 1);
            if (file.read(1) != "\n") {
                line.prepend('\n');
            }
        }
        if (!file.isOpen() || file.write(line) != line.size()) {
            qWarning().noquote() << "insights log append failed path=" + m_path
                                 << "error=" + file.errorString();
        }
    }
    emit changed();
}

bool InsightsLog::clear()
{
    if (m_access == Access::ReadWrite && QFile::exists(m_path) && !QFile::remove(m_path)) {
        qWarning().noquote() << "insights log could not be deleted path=" + m_path;
        return false;
    }
    m_records.clear();
    emit changed();
    return true;
}

} // namespace speecher
