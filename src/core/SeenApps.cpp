#include "core/SeenApps.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace speecher {

namespace {

const QString kId = QStringLiteral("applicationId");
const QString kName = QStringLiteral("name");
const QString kLastSeen = QStringLiteral("lastSeenMs");
const QString kDictations = QStringLiteral("dictations");

} // namespace

SeenApps::SeenApps(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    QFile file(m_path);
    if (m_path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject object = value.toObject();
        const SeenApp app{object.value(kId).toString(), object.value(kName).toString(),
                          qint64(object.value(kLastSeen).toDouble()), object.value(kDictations).toInt()};
        if (!app.applicationId.isEmpty()) {
            m_apps.append(app);
        }
    }
    std::stable_sort(m_apps.begin(), m_apps.end(),
                     [](const SeenApp &a, const SeenApp &b) { return a.lastSeenMs > b.lastSeenMs; });
}

const QList<SeenApp> &SeenApps::apps() const
{
    return m_apps;
}

void SeenApps::record(const DictationRecord &record)
{
    const QString id = record.applicationId.trimmed();
    if (id.isEmpty()) {
        return;
    }
    SeenApp seen{id, record.appName, record.finishedAt.toMSecsSinceEpoch(), 1};
    const auto known = std::find_if(m_apps.begin(), m_apps.end(),
                                    [&id](const SeenApp &app) { return app.applicationId == id; });
    if (known != m_apps.end()) {
        seen.dictations += known->dictations;
        if (seen.name.isEmpty()) {
            seen.name = known->name;
        }
        m_apps.erase(known);
    }
    m_apps.prepend(seen);
    if (m_apps.size() > capacity) {
        m_apps.resize(capacity);
    }
    save();
    emit changed();
}

bool SeenApps::clear()
{
    if (!m_path.isEmpty() && QFile::exists(m_path) && !QFile::remove(m_path)) {
        qWarning().noquote() << "seen apps could not be deleted path=" + m_path;
        return false;
    }
    m_apps.clear();
    emit changed();
    return true;
}

void SeenApps::save() const
{
    if (m_path.isEmpty()) {
        return;
    }
    QJsonArray array;
    for (const SeenApp &app : m_apps) {
        array.append(QJsonObject{{kId, app.applicationId}, {kName, app.name},
                                 {kLastSeen, double(app.lastSeenMs)}, {kDictations, app.dictations}});
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(array).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        qWarning().noquote() << "seen apps not saved path=" + m_path << "error=" + file.errorString();
    }
}

} // namespace speecher
