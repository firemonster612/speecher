#include "app/HistoryKeeper.h"

#include "core/SeenApps.h"
#include "core/SettingsStore.h"
#include "core/history/HistoryStore.h"
#include "dictation/DictationSession.h"
#include "recording/RecordingSession.h"
#include "recording/RecordingStatus.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribeQueue.h"

#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace speecher {

HistoryKeeper::HistoryKeeper(HistoryStore &history, SeenApps &seenApps, SettingsStore &settings,
                             DictationSession &session, FileTranscriptionSession &files, RecordingSession &recording,
                             bool seeded, QObject *parent)
    : QObject(parent)
    , m_history(history)
    , m_seenApps(seenApps)
    , m_settings(settings)
    , m_session(session)
    , m_files(files)
    , m_recording(recording)
    , m_seeded(seeded)
{
    connect(&session, &DictationSession::dictationFinished, this, &HistoryKeeper::keepDictation);
    connect(&files, &FileTranscriptionSession::fileFinished, this,
            [this](int, const TranscribeFileResult &result) { keepFile(result); });
    keepRecordings();
    // Retention runs at start, hourly, after each entry and as it changes.
    prune();
    auto *hourly = new QTimer(this);
    hourly->start(60 * 60 * 1000);
    connect(hourly, &QTimer::timeout, this, &HistoryKeeper::prune);
    connect(&settings, &SettingsStore::snapshotApplied, this, [this](const AppSettings &previous) {
        if (m_settings.dictationSnapshot().history != previous.history) {
            prune();
        }
    });
}

void HistoryKeeper::setTranscribeQueue(const TranscribeQueue *queue)
{
    m_transcribeQueue = queue;
}

qint64 HistoryKeeper::recordingHistoryId() const
{
    return m_recordingEntry;
}

void HistoryKeeper::keepRecordings()
{
    if (!m_seeded) {
        m_history.finishUnfinishedRecordings();
    }
    connect(&m_recording, &RecordingSession::started, this, [this](const QString &path, const QString &title) {
        HistoryEntry entry;
        entry.kind = HistoryKind::Recording;
        entry.startedAt = QDateTime::currentDateTime();
        entry.outcome = HistoryOutcome::Complete;
        entry.title = title;
        entry.transcriptPath = path;
        entry.kept = HistoryKeep::Text;
        m_recordingEntry = m_history.add(entry);
        for (const HistoryLine &line : std::exchange(m_startingRecordingLines, {})) {
            m_history.appendLine(m_recordingEntry, line);
        }
    });
    connect(&m_recording, &RecordingSession::lineWritten, this,
            [this](qint64 atMs, const QString &speaker, const QString &text) {
                if (m_recording.phase() == RecordingSession::Phase::Starting) {
                    m_startingRecordingLines.append({atMs, speaker, text});
                    return;
                }
                m_history.appendLine(m_recordingEntry, {atMs, speaker, text});
            });
    connect(&m_recording, &RecordingSession::phaseChanged, this, [this] {
        if (m_recording.phase() == RecordingSession::Phase::Off) {
            m_startingRecordingLines.clear();
        }
    });
    connect(&m_recording, &RecordingSession::stopped, this, [this](const RecordingStatus &status) {
        m_history.finishRecording(m_recordingEntry, QDateTime::currentDateTime(), status.durationMs,
                                  recordingEnd(status), status.audioPath);
    });
}

void HistoryKeeper::keepDictation(const HistoryEntry &entry, bool intoPasswordField)
{
    m_pasteAgainText = intoPasswordField ? QString() : m_session.lastTranscript();
    if (entry.kept == HistoryKeep::Nothing) {
        return;
    }
    m_history.add(entry);
    prune();
    if (entry.outcome == HistoryOutcome::Undelivered) {
        return;
    }
    m_seenApps.record(entry.stats);
    emit dictationKept(entry.stats);
}

// Its text while History keeps text, never its audio, which stays in the
// file.
void HistoryKeeper::keepFile(const TranscribeFileResult &result)
{
    const HistoryKeep keeps = std::min(m_settings.dictationSnapshot().history.keeps, HistoryKeep::Text);
    if (result.failed() || keeps == HistoryKeep::Nothing) {
        return;
    }
    HistoryEntry entry;
    entry.kind = HistoryKind::File;
    entry.outcome = HistoryOutcome::Done;
    entry.kept = keeps;
    entry.title = QFileInfo(result.path).fileName();
    entry.sourcePath = result.path;
    entry.transcriptPath = result.savedPath;
    entry.text = result.refined;
    entry.rawText = result.raw;
    entry.stats.finishedAt = QDateTime::currentDateTime();
    entry.stats.words = countWords(result.refined);
    entry.stats.profile = m_files.plan().profile.profile;
    if (m_transcribeQueue) {
        const qint64 durationMs = m_transcribeQueue->listedFile(result.path).durationMs;
        entry.stats.audioMs = int(std::clamp<qint64>(durationMs, 0, std::numeric_limits<int>::max()));
    }
    m_history.add(entry);
    prune();
}

QString HistoryKeeper::pasteAgainText() const
{
    if (m_pasteAgainText) {
        return *m_pasteAgainText;
    }
    if (m_settings.dictationSnapshot().history.keeps < HistoryKeep::Text) {
        return {};
    }
    HistoryQuery newest;
    newest.kinds = {HistoryKind::Dictation, HistoryKind::Edit};
    newest.limit = 1;
    const QList<HistoryHit> hits = m_history.search(newest);
    return hits.isEmpty() ? QString() : hits.first().entry.text.value_or(QString());
}

bool HistoryKeeper::clear(const QDateTime &from, const QDateTime &to, bool withStats)
{
    // The apps first: a file that won't go leaves History as it was.
    const bool everything = withStats && !from.isValid() && !to.isValid();
    if ((everything && !m_seenApps.clear()) || !m_history.clear(from, to, withStats)) {
        return false;
    }
    if (everything) {
        emit everythingCleared();
        m_pasteAgainText.reset();
    }
    return true;
}

void HistoryKeeper::prune()
{
    // A seed's entries keep their words.
    if (m_seeded) {
        return;
    }
    m_history.prune(QDateTime::currentDateTime(), m_settings.dictationSnapshot().history);
}

} // namespace speecher
