#pragma once

#include "core/Target.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace speecher {

// What one finished Dictation Session leaves behind for the stats on Home.
// Never the text or the audio.
struct DictationRecord {
    QDateTime finishedAt;
    int audioMs = 0;
    int words = 0;
    QString appName;
    WritingProfile profile = WritingProfile::Other;
};

// Words as a reader counts them: word-boundary segments that contain a letter
// or number, so punctuation is not a word and unspaced scripts still count.
int countWords(const QString &text);

// The local log behind Home, one JSON object per line:
// {"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Thunderbird","profile":"email"}
// finishedAt is local time without a zone; profile is writingProfileName().
class InsightsLog : public QObject {
    Q_OBJECT

public:
    enum class Access {
        ReadWrite,
        // A seed file for screenshots and demos: loaded, never written or
        // deleted. New records live in memory until the process exits.
        ReadOnly,
    };

    explicit InsightsLog(const QString &path,
                         Access access = Access::ReadWrite,
                         QObject *parent = nullptr);

    const QList<DictationRecord> &records() const;
    void append(const DictationRecord &record);
    // Forgets every record and deletes the file (a read-only seed is kept).
    void clear();

signals:
    void changed();

private:
    QString m_path;
    Access m_access;
    QList<DictationRecord> m_records;
};

} // namespace speecher
