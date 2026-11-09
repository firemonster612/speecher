#pragma once

// What History holds: one entry per dictation, selection edit, transcribed
// file or Recording, how much of it was kept, and the questions a search asks.

#include "core/DictationRecord.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace speecher {

enum class HistoryKind { Dictation, Edit, File, Recording };

// What Speecher Keeps, ordered: a lower level keeps less, so the strictest of
// several rules is their minimum.
enum class HistoryKeep { Nothing, Stats, Text, Audio };

// What became of an entry's words. A dictation is Delivered, Copied (the
// receipt said the words only reached the clipboard) or Undelivered; a file
// transcription is Done; a Recording is Complete, or Missed when its
// RecordingEnd says it missed speech.
enum class HistoryOutcome { Delivered, Copied, Undelivered, Done, Complete, Missed };

// How a Recording ended and what it missed on the way, kept with its entry so
// a warning outlives the recording.
struct RecordingEnd {
    // Stopped by the person, after Stop after silence, or by Speecher
    // quitting while it ran.
    enum class Reason { Stopped, Silence, Interrupted };
    // A side that stopped for good before the end: who, when from the start,
    // and why.
    struct StreamStop {
        QString speaker;
        qint64 atMs = 0;
        QString problem;

        bool operator==(const StreamStop &other) const = default;
    };

    Reason reason = Reason::Stopped;
    // Utterances the speech service didn't transcribe, audio dropped while a
    // stream was down, and lines the file didn't take.
    int missedUtterances = 0;
    qint64 lostAudioMs = 0;
    int unwrittenLines = 0;
    QList<StreamStop> streamStops;
    // Why its audio, asked for, wasn't saved; empty when it was.
    QString audioError;

    bool missedSpeech() const
    {
        return missedUtterances > 0 || lostAudioMs > 0 || unwrittenLines > 0 || !streamStops.isEmpty();
    }
    bool operator==(const RecordingEnd &other) const = default;
};

struct HistoryEntry {
    qint64 id = 0;
    HistoryKind kind = HistoryKind::Dictation;
    // finishedAt, audioMs, words, app, profile and services. Its
    // applicationId is kept only with the text.
    DictationRecord stats;
    QDateTime startedAt;
    HistoryOutcome outcome = HistoryOutcome::Delivered;
    // A file entry's file name; a Recording's name, empty when it has none.
    QString title;
    // The Refined and Raw Transcripts; nullopt when they weren't kept or
    // their retention ended.
    std::optional<QString> text;
    std::optional<QString> rawText;
    // The audio file, relative to the store's audio folder; empty without one.
    QString audioPath;
    // A file entry's input.
    QString sourcePath;
    // A file entry's saved transcript; a Recording's file.
    QString transcriptPath;
    // What was allowed when it was written, lowered as retention removes parts.
    HistoryKeep kept = HistoryKeep::Stats;
    // A finished Recording's end.
    RecordingEnd end;
};

// One page of History, newest first. Every field narrows; together they AND.
struct HistoryQuery {
    // Words that must all appear, in any order, as any part of the text, raw
    // text, app name or title, ignoring case. A part in double quotes must
    // appear as written.
    QString text;
    // Empty: every kind.
    QList<HistoryKind> kinds;
    // The app's name as entries record it; empty: any.
    QString app;
    // A Writing Profile's id; empty: any.
    QString profile;
    // Finished at or after from, and before to; invalid: open.
    QDateTime from;
    QDateTime to;
    int limit = 100;
    // The last entry of the previous page, for the next one; 0: the first page.
    qint64 beforeId = 0;
};

// One line of a Recording: when its text was finalised, from the start, who
// said it (me, them) and what.
struct HistoryLine {
    qint64 atMs = 0;
    QString speaker;
    QString text;

    bool operator==(const HistoryLine &other) const = default;
};

struct HistoryHit {
    HistoryEntry entry;
    // The entry's text with each match between \x02 and \x03.
    QString highlighted;
};

// "hh:mm:ss" since a recording started, for its lines and its duration.
QString recordingClock(qint64 elapsedMs);
// line as a Recording's plain text file has it, without the line break:
// "[00:00:04] me: Can you see my screen?". parseTranscriptLine reads one back,
// trimmed; none where it is anything else.
QString transcriptLineText(const HistoryLine &line);
std::optional<HistoryLine> parseTranscriptLine(const QString &line);

QString historyKindName(HistoryKind kind);
std::optional<HistoryKind> historyKindFromName(const QString &name);
QString historyKeepName(HistoryKeep keep);
std::optional<HistoryKeep> historyKeepFromName(const QString &name);
QString historyOutcomeName(HistoryOutcome outcome);
std::optional<HistoryOutcome> historyOutcomeFromName(const QString &name);
// How the store and the UI carry a RecordingEnd: { reason (stopped, silence,
// interrupted), missedUtterances, lostAudioMs, unwrittenLines, streamStops:
// [{ speaker, atMs, problem }], audioError }, a count left out at 0.
QJsonObject recordingEndJson(const RecordingEnd &end);
RecordingEnd recordingEndFromJson(const QJsonObject &object);

// The parts of a search a match needs, each folded to lower case: the quoted
// phrases as written, then the other words.
QStringList historySearchTerms(const QString &text);
// text with each case-insensitive occurrence of a term between \x02 and \x03;
// overlapping occurrences merge into one mark.
QString highlightHistoryMatches(const QString &text, const QStringList &terms);

} // namespace speecher

Q_DECLARE_METATYPE(speecher::HistoryEntry)
Q_DECLARE_METATYPE(speecher::HistoryKind)
Q_DECLARE_METATYPE(speecher::HistoryKeep)
Q_DECLARE_METATYPE(speecher::HistoryOutcome)
