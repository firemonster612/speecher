#pragma once

#include "core/DictationRecord.h"

#include <QList>
#include <QObject>
#include <QString>

// The apps dictated into lately, for the Apps page's "Recently dictated into".
// It hears the same records the insights log does, so it records only while
// Insights is on, and Clear insights empties it: app names and ids, never
// text. Kept as seen-apps.json beside the insights log.

namespace speecher {

struct SeenApp {
    // The Target's application id, which an Id match compares.
    QString applicationId;
    // As the dictation's record named the app.
    QString name;
    qint64 lastSeenMs = 0;
    int dictations = 0;

    bool operator==(const SeenApp &other) const = default;
};

class SeenApps : public QObject {
    Q_OBJECT

public:
    // At most this many, the least recent dropped first.
    static constexpr int capacity = 100;

    // An empty path keeps the apps in memory only, as for an insights seed.
    explicit SeenApps(const QString &path, QObject *parent = nullptr);

    // Most recent first.
    const QList<SeenApp> &apps() const;
    // Counts a dictation into the record's app; a record without an app id
    // counts nothing.
    void record(const DictationRecord &record);
    // Deletes the file, then forgets every app. False, with nothing
    // forgotten, when the file could not be deleted.
    bool clear();

signals:
    void changed();

private:
    void save() const;

    QString m_path;
    QList<SeenApp> m_apps;
};

} // namespace speecher
