#pragma once

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
// delete with Undo, clear). Data only; pages/history/HistoryWords.js words
// it. Times reach QML as Dates; a profile is its id with its name.
class QuickHistory final : public QObject, public HostSingleton<QuickHistory> {
    Q_OBJECT
    QML_NAMED_ELEMENT(History)
    QML_SINGLETON
    // { text, kind (all, dictation, file), app, profile, when (any, today,
    // yesterday, 7d, 30d, year) }; a part left out is any. Writing it
    // searches once typing pauses (searchDelayMs).
    Q_PROPERTY(QVariantMap query READ query WRITE setQuery NOTIFY queryChanged)
    // The entries found, newest first, a page at a time: [{ id, kind, day
    // ("yyyy-MM-dd", local), finishedAt, text, html (the text, HTML-escaped,
    // each match in <b>), app, appId, profile, profileName, durationMs, words,
    // outcome (delivered, copied, undelivered, done), hasAudio, hasRaw,
    // title }].
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    // How many the query finds on every page, and whether there are more to
    // fetch.
    Q_PROPERTY(int matches READ matches NOTIFY resultsChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY resultsChanged)
    // A changed query waits to be searched.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // How many entries hold text at all, whatever the query.
    Q_PROPERTY(int kept READ kept NOTIFY changed)
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
    QStringList apps() const;
    QVariantList profiles() const;
    QVariantMap playing() const;

    // The next page of the query's entries.
    Q_INVOKABLE void fetchMore();
    // One entry in full: what a result holds, with raw (the Raw
    // Transcript), speech and refinement (the services that ran, by id) and
    // sourcePath.
    // Empty for one that's gone.
    Q_INVOKABLE QVariantMap entry(qint64 id) const;
    // The entry's Refined Transcript, or with raw its Raw Transcript, on the
    // clipboard, or pasted into the app in front.
    Q_INVOKABLE void copy(qint64 id, bool raw);
    Q_INVOKABLE void pasteAgain(qint64 id, bool raw);
    // Hides the entry at once; undo(token) brings it back until undoMs
    // passes. Returns the token, or -1 when nothing went.
    Q_INVOKABLE int remove(qint64 id);
    Q_INVOKABLE bool undo(int token);
    // Plays the entry's audio, stopping any other.
    Q_INVOKABLE void play(qint64 id);
    Q_INVOKABLE void stopPlaying();
    // hour, today, 7d or all: how many dictations and files finished then,
    // and deleting their words (and with withStats, the entries).
    Q_INVOKABLE int countIn(const QString &range) const;
    Q_INVOKABLE bool clear(const QString &range, bool withStats);

signals:
    void queryChanged();
    void resultsChanged();
    void searchingChanged();
    void changed();
    void playingChanged();

private:
    HistoryQuery currentQuery() const;
    void search(int limit);
    QVariantMap resultOf(const HistoryHit &hit) const;

    ApplicationController *m_controller;
    QVariantMap m_query;
    QVariantList m_results;
    int m_matches = 0;
    QTimer m_searchDelay;
    QTimer m_purge;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    qint64 m_playingId = 0;
};

} // namespace speecher
