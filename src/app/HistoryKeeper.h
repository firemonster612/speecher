#pragma once

// Writes History as Speecher runs: each finished dictation, transcribed file
// and Recording becomes an entry, retention runs on them, and the apps
// dictated into are noted. It also answers what Paste Again pastes and
// clears History once the person has confirmed it.

#include "core/DictationRecord.h"
#include "core/history/HistoryEntry.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <optional>

namespace speecher {

class DictationSession;
class FileTranscriptionSession;
class HistoryStore;
class RecordingSession;
class SeenApps;
class SettingsStore;
class TranscribeQueue;
struct TranscribeFileResult;

class HistoryKeeper : public QObject {
    Q_OBJECT

public:
    // seeded: History runs on a seed, in memory, whose entries retention
    // leaves be and whose unfinished Recordings stay as they are. Otherwise
    // it finishes the Recordings a crash left unfinished, and runs retention
    // now, hourly and whenever its settings change.
    HistoryKeeper(HistoryStore &history, SeenApps &seenApps, SettingsStore &settings, DictationSession &session,
                  FileTranscriptionSession &files, RecordingSession &recording, bool seeded,
                  QObject *parent = nullptr);

    // The Transcribe page's queue, made on first use, which knows how long
    // each file it lists runs.
    void setTranscribeQueue(const TranscribeQueue *queue);

    // The running or last recording's History Entry, 0 for none.
    qint64 recordingHistoryId() const;
    // What Paste Again pastes: History's newest dictation while it keeps
    // text, unless one this run is newer; while it doesn't, the last one
    // this run. Never one into a password field. Empty when there's none.
    QString pasteAgainText() const;
    // Clears the words and audio of what finished in [from, to) (an invalid
    // bound is open), and with withStats the entries, and for everything the
    // apps dictated into too. False when a file refused it.
    bool clear(const QDateTime &from, const QDateTime &to, bool withStats);

signals:
    // A delivered dictation History kept, as its stats record it.
    void dictationKept(const DictationRecord &record);
    // Everything was cleared, the apps dictated into included.
    void everythingCleared();

private:
    void keepDictation(const HistoryEntry &entry, bool intoPasswordField);
    // A file the Transcribe page finished, as an entry of kind File.
    void keepFile(const TranscribeFileResult &result);
    // A Recording is a History Entry while it runs: added as it starts, a
    // line at a time, finished as it stops.
    void keepRecordings();
    // Retention, as of now; nothing for a seed.
    void prune();

    HistoryStore &m_history;
    SeenApps &m_seenApps;
    SettingsStore &m_settings;
    DictationSession &m_session;
    FileTranscriptionSession &m_files;
    RecordingSession &m_recording;
    const TranscribeQueue *m_transcribeQueue = nullptr;
    const bool m_seeded;
    qint64 m_recordingEntry = 0;
    // Lines written while the recording starts, before its History Entry
    // exists; started replays them into it, and a start that fails drops them.
    QList<HistoryLine> m_startingRecordingLines;
    // The last dictation this run, for Paste Again: nullopt before the
    // first, empty for one into a password field.
    std::optional<QString> m_pasteAgainText;
};

} // namespace speecher
