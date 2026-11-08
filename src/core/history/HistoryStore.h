#pragma once

// History on disk: every entry in one SQLite file, searched through an FTS5
// trigram index, with the dictations' audio in a folder beside it. It also
// keeps what each entry may hold: an entry never keeps more than its `kept`.

#include "core/history/HistoryEntry.h"
#include "core/history/HistoryPolicy.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

class HistoryStore : public QObject {
    Q_OBJECT

public:
    // Opens databasePath, creating it closed to other users, or ":memory:"
    // for a History that goes when the process does, as a seed's does.
    // audioFolder holds dictation audio; an entry's audioPath is relative to
    // it. Entries deleted and not restored before the last run are purged.
    HistoryStore(const QString &databasePath, const QString &audioFolder, QObject *parent = nullptr);
    ~HistoryStore() override;

    // False when the file couldn't be opened or created; every call then
    // keeps and finds nothing.
    bool isOpen() const;
    // Whether search goes through the trigram index. Without it (an SQLite
    // older than 3.34) every search scans the entries with instr, matching
    // ASCII letters of either case only.
    bool searchesByTrigram() const;
    // Where an entry's audio file is; empty when it has none.
    QString audioFile(const HistoryEntry &entry) const;

    // Adds an entry, dropping what its `kept` doesn't allow: text, raw text
    // and the app's id below Text, audio below Audio. Returns its id, or 0
    // when it couldn't be written.
    qint64 add(const HistoryEntry &entry);
    // Adds every entry or none, as add does each, oldest first.
    bool addAll(const QList<HistoryEntry> &entries);

    // One page of the entries that still hold text, newest first: in the
    // order they came in, which is the order they finished in. (A page in
    // time order would make the index find every match before cutting it.)
    QList<HistoryHit> search(const HistoryQuery &query) const;
    // How many entries the query finds over every page.
    int count(const HistoryQuery &query) const;
    std::optional<HistoryEntry> entry(qint64 id) const;
    // The app names and profile ids among the entries that hold text, most
    // used first, for search's filters.
    QStringList appNames() const;
    QStringList profiles() const;

    // A Recording's lines, oldest first. appendLine adds one as its file gets
    // it; finishRecording ends the entry once the recording stops: when,
    // its length, how it ended (Missed where that missed speech, else
    // Complete) and its audio file (empty for none), with its text and word
    // count taken from its lines, which search then finds. Neither says
    // changed for a line, which comes many times a minute.
    bool appendLine(qint64 entryId, const HistoryLine &line);
    QList<HistoryLine> lines(qint64 entryId) const;
    bool finishRecording(qint64 id, const QDateTime &finishedAt, qint64 audioMs, const RecordingEnd &end,
                         const QString &audioPath);
    // Finishes the Recordings a crash left running, Interrupted, at their
    // file's last change, or their start where the file is gone.
    void finishUnfinishedRecordings();
    // Takes a finished Recording's lines, text and word count from its file
    // again once the file changed since, as an edit in Open file changes it,
    // so the reader, export and search say what the file says. False when
    // the file changed into something other than lines ("[00:00:04] me:
    // …"), which leaves History's lines as they were; true while they are
    // in step, or there is no file to be in step with.
    bool syncRecordingFile(qint64 id);
    // Sets a dictation's, edit's or file's Refined Transcript, keeping its raw
    // one, as Refine again's Keep this does. False for an entry without text.
    bool setText(qint64 id, const QString &text);

    // Deletes an entry and its lines at once, leaving its files, for a
    // Recording, whose transcript and audio the person chooses to keep or not.
    bool remove(qint64 id);
    // Deletes a Recording's transcript and audio files, returning those that
    // wouldn't go, so its entry can stay until they do.
    QStringList deleteRecordingFiles(const HistoryEntry &entry) const;
    // Hides an entry from every question until restore or purgeDeleted.
    bool markDeleted(qint64 id);
    bool restore(qint64 id);
    // Deletes hidden entries and their audio for good. One whose audio file
    // wouldn't go stays hidden, for the next purge to try again.
    void purgeDeleted();

    // How many dictations, edits and files finished in [from, to); an invalid
    // bound is open. Recordings never count.
    int countIn(const QDateTime &from, const QDateTime &to) const;
    // Deletes their text, raw text, app ids and audio, and with withStats the
    // entries themselves, overwriting what was deleted. An entry whose audio
    // file wouldn't go stays as it was. False when the file refused it or
    // such an entry stayed.
    bool clear(const QDateTime &from, const QDateTime &to, bool withStats);
    // Applies retention as of now: dictations and files older than textDays
    // keep only their stats, and their audio older than audioDays (never
    // longer than text) goes, except audio whose file wouldn't go, which the
    // next prune tries again. Recordings keep their text and audio until
    // deleted.
    void prune(const QDateTime &now, const HistorySettings &settings);

    // Home's stats: every delivered dictation and edit, oldest first.
    const QList<DictationRecord> &records() const;

signals:
    // Something a question would answer differently.
    void changed();

private:
    bool exec(const QString &sql) const;
    void createSchema();
    void prepareSearchIndex();
    // Deletes the audio files of the entries condition finds, returning the
    // ids of those whose file wouldn't go.
    QList<qint64> deleteAudioWhere(const QString &condition, const QVariantList &values);
    // True once file is gone, or never was.
    static bool deleteFile(const QString &file);
    void forgetRecords();

    QString m_connection;
    QString m_audioFolder;
    bool m_open = false;
    bool m_trigram = false;
    mutable std::optional<QList<DictationRecord>> m_records;
};

} // namespace speecher
