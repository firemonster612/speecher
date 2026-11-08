#pragma once

// What History holds: one entry per dictation, selection edit, transcribed
// file or Recording, how much of it was kept, and the questions a search asks.

#include "core/DictationRecord.h"

#include <QDateTime>
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
// transcription is Done; a Recording is Complete, or Missed when its file
// lacks lines or a stream lost audio.
enum class HistoryOutcome { Delivered, Copied, Undelivered, Done, Complete, Missed };

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

QString historyKindName(HistoryKind kind);
std::optional<HistoryKind> historyKindFromName(const QString &name);
QString historyKeepName(HistoryKeep keep);
std::optional<HistoryKeep> historyKeepFromName(const QString &name);
QString historyOutcomeName(HistoryOutcome outcome);
std::optional<HistoryOutcome> historyOutcomeFromName(const QString &name);

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
