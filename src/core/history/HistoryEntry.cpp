#include "core/history/HistoryEntry.h"

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
