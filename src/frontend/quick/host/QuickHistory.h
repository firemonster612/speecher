#pragma once

#include "app/RefineAgain.h"
#include "core/history/HistoryEntry.h"
#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariant>

class QAudioOutput;
class QMediaPlayer;

namespace speecher {

class ApplicationController;

// The UI's History singleton: what the History page lists and searches, an
// entry in full, and what the page does with one (copy, Paste Again, play,
// Refine again, delete with Undo, clear; for a Recording, read, open, export
// and delete with its files). Data only; pages/history/HistoryWords.js words
// it. Times reach QML as Dates; a profile is its id with its name.
class QuickHistory final : public QObject, public HostSingleton<QuickHistory> {
    Q_OBJECT
    QML_NAMED_ELEMENT(History)
    QML_SINGLETON
    // { text, kind (all, dictation, file, recording), app, profile, when (any, today,
    // yesterday, 7d, 30d, year) }; a part left out is any. Writing it
    // searches once typing pauses (searchDelayMs), first taking the lines of
    // each Recording whose file was edited from that file again.
    Q_PROPERTY(QVariantMap query READ query WRITE setQuery NOTIFY queryChanged)
    // The entries found, newest first, a page at a time: [{ id, kind, day
    // ("yyyy-MM-dd", local), startedAt, finishedAt, text, html (the text,
    // HTML-escaped, each match in <b>), app, appId, profile, profileName,
    // durationMs, words, outcome (delivered, copied, undelivered, done,
    // complete, missed), hasAudio, hasRaw, title, transcriptPath, fileMissing
    // (a Recording's or a file's saved transcript is gone), sourceMissing (a
    // file's audio is gone), live (a Recording still running, which has no
    // finishedAt), ending (a finished Recording's, as recordingEndJson),
    // lines (a Recording's lines that match the search, at most three: {
    // atMs, speaker, text }) }].
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    // How many the query finds on every page, and whether there are more to
    // fetch.
    Q_PROPERTY(int matches READ matches NOTIFY resultsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY resultsChanged)
    // A changed query waits to be searched.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // How many entries hold text at all, whatever the query, and how many of
    // them are dictations and files, which clearing and keeping less delete.
    Q_PROPERTY(int kept READ kept NOTIFY changed)
    Q_PROPERTY(int keptText READ keptText NOTIFY changed)
    // The filters' choices among the entries that hold text, most used
    // first: app names, and [{ id, name }] profiles.
    Q_PROPERTY(QStringList apps READ apps NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    // { id, positionMs, durationMs } of the entry playing, or empty.
    Q_PROPERTY(QVariantMap playing READ playing NOTIFY playingChanged)

public:
    explicit QuickHistory(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickHistory() override;

    static constexpr int searchDelayMs = 120;
    static constexpr int pageSize = 100;
    // How long a deleted entry waits for its Undo before it goes for good.
    static constexpr int undoMs = 30000;

    QVariantMap query() const { return m_query; }
    void setQuery(const QVariantMap &query);
    QVariantList results() const { return m_results; }
    int matches() const { return m_matches; }
    bool hasMore() const { return m_results.size() < m_matches; }
    bool searching() const { return m_searchDelay.isActive(); }
    int kept() const;
    int keptText() const;
    QStringList apps() const;
    QVariantList profiles() const;
    QVariantMap playing() const;

    // The next page of the query's entries.
    Q_INVOKABLE void fetchMore();
    // The newest entries of a kind (dictation, file, recording), as results
    // hold them, whatever the query: Recordings' Recent recordings. Ask
    // again on changed.
    Q_INVOKABLE QVariantList recent(const QString &kind, int limit) const;
    // One entry in full: what a result holds, with raw (the Raw
    // Transcript), speech and refinement (the services that ran, by id),
    // sourcePath and sourceUrl, and a Recording's lines ([{ atMs, speaker,
    // text }]), inRecordingsFolder (its file is in the folder Recordings
    // saves to) and fileDiverges (its file was changed into something
    // History can't read as lines). A Recording's lines are first taken
    // again from its file where that was edited. Empty for one that's gone.
    Q_INVOKABLE QVariantMap entry(qint64 id) const;
    // The parts of a search as History matches them, quoted phrases whole,
    // for a reader to find the same.
    Q_INVOKABLE QStringList searchTerms(const QString &text) const;
    // The entry's Refined Transcript, or with raw its Raw Transcript, on the
    // clipboard, or pasted into the app in front. A Recording copies its
    // lines as its plain text export has them.
    Q_INVOKABLE void copy(qint64 id, bool raw);
    Q_INVOKABLE void pasteAgain(qint64 id, bool raw);
    // Hides the entry at once; undo(token) brings it back until undoMs
    // passes. Returns the token, or -1 when nothing went.
    Q_INVOKABLE int remove(qint64 id);
    Q_INVOKABLE bool undo(int token);
    // Deletes a Recording at once, with withFiles its transcript and audio
    // files first. Returns the files that wouldn't go, whose entry then
    // stays, so trying again or deleting without them can follow.
    Q_INVOKABLE QStringList removeRecording(qint64 id, bool withFiles);
    // Plays the entry's audio from atMs, stopping any other.
    Q_INVOKABLE void play(qint64 id, qint64 atMs = 0);
    Q_INVOKABLE void stopPlaying();
    // A Recording's file, or a file entry's saved transcript, in the
    // desktop's editor, or its folder; a file entry with no saved
    // transcript shows its audio's folder.
    Q_INVOKABLE void openFile(qint64 id) const;
    Q_INVOKABLE void showInFolder(qint64 id) const;
    // Where Export offers to save a Recording as md, txt or json: beside its
    // transcript, named after it.
    Q_INVOKABLE QUrl exportFile(qint64 id, const QString &format) const;
    // Saves a Recording's lines to file, taken again from its file where
    // that was edited: md and txt as its file has them, json as [{ atMs,
    // speaker, text }]. False when the file can't be written.
    Q_INVOKABLE bool exportEntry(qint64 id, const QString &format, const QUrl &file) const;
    // Refines the entry's Raw Transcript again with a Writing Profile, as a
    // dictation into no app would; refined(id, text) or refineFailed(id,
    // message) answers. keepRefined makes text the entry's Refined
    // Transcript.
    Q_INVOKABLE void refineAgain(qint64 id, const QString &profile);
    Q_INVOKABLE bool keepRefined(qint64 id, const QString &text);
    // hour, today, 7d or all: how many dictations and files finished then,
    // and deleting their words (and with withStats, the entries). clear
    // answers { cleared, left, files }: how many that held text were
    // cleared, how many still hold it, and the audio files that wouldn't go
    // and kept theirs. left is 0 once it all went.
    Q_INVOKABLE int countIn(const QString &range) const;
    Q_INVOKABLE QVariantMap clear(const QString &range, bool withStats);

signals:
    void queryChanged();
    void resultsChanged();
    void searchingChanged();
    void changed();
    void playingChanged();
    void refined(qint64 id, const QString &text);
    void refineFailed(qint64 id, const QString &message);

private:
    HistoryQuery currentQuery() const;
    void search(int limit);
    // Takes each finished Recording's lines from its file again where the
    // file changed, so a search finds the words the file now holds.
    void syncRecordingFiles();
    // terms: the search's, which pick a Recording's matching lines.
    QVariantMap resultOf(const HistoryHit &hit, const QStringList &terms = {}) const;

    ApplicationController *m_controller;
    QVariantMap m_query;
    QVariantList m_results;
    int m_matches = 0;
    QTimer m_searchDelay;
    QTimer m_purge;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    qint64 m_playingId = 0;
    RefineAgain m_refineAgain;
};

} // namespace speecher
