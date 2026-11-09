#include "core/history/HistoryEntry.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <utility>

namespace speecher {
namespace {

// The names the database and the UI use, which must never change once stored.
constexpr std::array<std::pair<HistoryKind, const char *>, 4> kindNames{{
    {HistoryKind::Dictation, "dictation"},
    {HistoryKind::Edit, "edit"},
    {HistoryKind::File, "file"},
    {HistoryKind::Recording, "recording"},
}};
constexpr std::array<std::pair<HistoryKeep, const char *>, 4> keepNames{{
    {HistoryKeep::Nothing, "nothing"},
    {HistoryKeep::Stats, "stats"},
    {HistoryKeep::Text, "text"},
    {HistoryKeep::Audio, "audio"},
}};
constexpr std::array<std::pair<HistoryOutcome, const char *>, 6> outcomeNames{{
    {HistoryOutcome::Delivered, "delivered"},
    {HistoryOutcome::Copied, "copied"},
    {HistoryOutcome::Undelivered, "undelivered"},
    {HistoryOutcome::Done, "done"},
    {HistoryOutcome::Complete, "complete"},
    {HistoryOutcome::Missed, "missed"},
}};
constexpr std::array<std::pair<RecordingEnd::Reason, const char *>, 3> endReasonNames{{
    {RecordingEnd::Reason::Stopped, "stopped"},
    {RecordingEnd::Reason::Silence, "silence"},
    {RecordingEnd::Reason::Interrupted, "interrupted"},
}};

template<typename Value, std::size_t size>
QString nameOf(const std::array<std::pair<Value, const char *>, size> &names, Value value)
{
    for (const auto &[each, name] : names) {
        if (each == value) {
            return QLatin1String(name);
        }
    }
    return {};
}

template<typename Value, std::size_t size>
std::optional<Value> valueNamed(const std::array<std::pair<Value, const char *>, size> &names, const QString &name)
{
    for (const auto &[value, each] : names) {
        if (name == QLatin1String(each)) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace

QString historyKindName(HistoryKind kind)
{
    return nameOf(kindNames, kind);
}

std::optional<HistoryKind> historyKindFromName(const QString &name)
{
    return valueNamed(kindNames, name);
}

QString historyKeepName(HistoryKeep keep)
{
    return nameOf(keepNames, keep);
}

std::optional<HistoryKeep> historyKeepFromName(const QString &name)
{
    return valueNamed(keepNames, name);
}

QString historyOutcomeName(HistoryOutcome outcome)
{
    return nameOf(outcomeNames, outcome);
}

std::optional<HistoryOutcome> historyOutcomeFromName(const QString &name)
{
    return valueNamed(outcomeNames, name);
}

QJsonObject recordingEndJson(const RecordingEnd &end)
{
    QJsonObject object{{QStringLiteral("reason"), nameOf(endReasonNames, end.reason)}};
    const auto count = [&object](const QString &key, qint64 value) {
        if (value > 0) {
            object.insert(key, value);
        }
    };
    count(QStringLiteral("missedUtterances"), end.missedUtterances);
    count(QStringLiteral("lostAudioMs"), end.lostAudioMs);
    count(QStringLiteral("unwrittenLines"), end.unwrittenLines);
    QJsonArray stops;
    for (const RecordingEnd::StreamStop &stop : end.streamStops) {
        stops.append(QJsonObject{{QStringLiteral("speaker"), stop.speaker},
                                 {QStringLiteral("atMs"), stop.atMs},
                                 {QStringLiteral("problem"), stop.problem}});
    }
    if (!stops.isEmpty()) {
        object.insert(QStringLiteral("streamStops"), stops);
    }
    if (!end.audioError.isEmpty()) {
        object.insert(QStringLiteral("audioError"), end.audioError);
    }
    return object;
}

RecordingEnd recordingEndFromJson(const QJsonObject &object)
{
    RecordingEnd end;
    end.reason = valueNamed(endReasonNames, object.value(QStringLiteral("reason")).toString())
                     .value_or(RecordingEnd::Reason::Stopped);
    end.missedUtterances = object.value(QStringLiteral("missedUtterances")).toInt();
    end.lostAudioMs = object.value(QStringLiteral("lostAudioMs")).toInteger();
    end.unwrittenLines = object.value(QStringLiteral("unwrittenLines")).toInt();
    for (const QJsonValue &value : object.value(QStringLiteral("streamStops")).toArray()) {
        const QJsonObject stop = value.toObject();
        end.streamStops.append({stop.value(QStringLiteral("speaker")).toString(),
                                stop.value(QStringLiteral("atMs")).toInteger(),
                                stop.value(QStringLiteral("problem")).toString()});
    }
    end.audioError = object.value(QStringLiteral("audioError")).toString();
    return end;
}

QStringList historySearchTerms(const QString &text)
{
    // A quoted phrase, closed or running to the end, or a run of non-space.
    static const QRegularExpression part(QStringLiteral(R"re("([^"]*)"?|(\S+))re"));
    QStringList terms;
    for (const QRegularExpressionMatch &match : part.globalMatch(text)) {
        const QString term = (match.hasCaptured(1) ? match.captured(1) : match.captured(2)).toLower();
        if (!term.trimmed().isEmpty() && !terms.contains(term)) {
            terms.append(term);
        }
    }
    return terms;
}

QString recordingClock(qint64 elapsedMs)
{
    const qint64 seconds = elapsedMs / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString transcriptLineText(const HistoryLine &line)
{
    return QStringLiteral("[%1] %2: %3").arg(recordingClock(line.atMs), line.speaker, line.text);
}

std::optional<HistoryLine> parseTranscriptLine(const QString &line)
{
    static const QRegularExpression lineFormat(QStringLiteral(R"(^\[(\d+):(\d{2}):(\d{2})\] ([^:]+): (.*)$)"));
    const QRegularExpressionMatch match = lineFormat.match(line);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    const qint64 seconds = (match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 60
        + match.captured(3).toLongLong();
    return HistoryLine{seconds * 1000, match.captured(4), match.captured(5)};
}

QString highlightHistoryMatches(const QString &text, const QStringList &terms)
{
    // Marked characters, so overlapping and adjacent matches become one mark.
    QList<bool> marked(text.size(), false);
    for (const QString &term : terms) {
        for (qsizetype at = text.indexOf(term, 0, Qt::CaseInsensitive); at >= 0;
             at = text.indexOf(term, at + 1, Qt::CaseInsensitive)) {
            std::fill(marked.begin() + at, marked.begin() + at + term.size(), true);
        }
    }
    QString highlighted;
    highlighted.reserve(text.size() + 8);
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (marked[i] && (i == 0 || !marked[i - 1])) {
            highlighted += QChar(0x02);
        }
        highlighted += text[i];
        if (marked[i] && (i + 1 == text.size() || !marked[i + 1])) {
            highlighted += QChar(0x03);
        }
    }
    return highlighted;
}

} // namespace speecher
