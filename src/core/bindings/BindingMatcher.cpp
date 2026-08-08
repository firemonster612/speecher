#include "core/bindings/BindingMatcher.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <functional>

namespace speecher {
namespace {

struct Token {
    QString text;
    qsizetype start = 0;
    qsizetype end = 0;
};

struct CompiledRule {
    BindingRule rule;
    QStringList tokens;
    int order = 0;
};

struct Candidate {
    int ruleIndex = 0;
    int startToken = 0;
    int tokenCount = 0;
};

QList<Token> tokenizeWithSpans(const QString &text)
{
    QList<Token> tokens;
    QString current;
    qsizetype tokenStart = -1;
    const auto flush = [&](qsizetype end) {
        if (current.isEmpty()) return;
        tokens.append({current.toCaseFolded(), tokenStart, end});
        current.clear();
        tokenStart = -1;
    };
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch.isLetterOrNumber()) {
            if (current.isEmpty()) tokenStart = index;
            current.append(ch);
        } else {
            flush(index);
        }
    }
    flush(text.size());
    return tokens;
}

QList<CompiledRule> compileRules(const QList<BindingRule> &rules)
{
    QList<CompiledRule> compiled;
    QSet<QString> seen;
    for (int index = 0; index < rules.size(); ++index) {
        const BindingRule &rule = rules.at(index);
        const QString phrase = rule.phrase.trimmed();
        const QStringList tokens = BindingMatcher::normalizedTokens(phrase);
        const QString normalized = tokens.join(QStringLiteral(" "));
        if (phrase.isEmpty() || rule.replacement.trimmed().isEmpty() || tokens.isEmpty() || seen.contains(normalized)) continue;
        seen.insert(normalized);
        compiled.append({BindingRule{phrase, rule.replacement}, tokens, index});
    }
    return compiled;
}

bool sequenceMatches(const QList<Token> &transcriptTokens, int start, const QStringList &ruleTokens)
{
    if (start + ruleTokens.size() > transcriptTokens.size()) return false;
    for (int offset = 0; offset < ruleTokens.size(); ++offset) {
        if (transcriptTokens.at(start + offset).text != ruleTokens.at(offset)) return false;
    }
    return true;
}

QString replaceMatches(const QString &transcript, const QList<BindingMatch> &matches,
                       const std::function<QString(int, const BindingMatch &)> &replacement)
{
    QString output;
    qsizetype cursor = 0;
    for (int index = 0; index < matches.size(); ++index) {
        const BindingMatch &match = matches.at(index);
        output += transcript.mid(cursor, match.start - cursor);
        output += replacement(index, match);
        cursor = match.end;
    }
    output += transcript.mid(cursor);
    return output;
}

bool allTranscriptWordsCovered(const QString &transcript, const QList<BindingMatch> &matches)
{
    if (matches.isEmpty()) return false;
    int matchIndex = 0;
    for (qsizetype index = 0; index < transcript.size(); ++index) {
        if (!transcript.at(index).isLetterOrNumber()) continue;
        while (matchIndex < matches.size() && matches.at(matchIndex).end <= index) ++matchIndex;
        if (matchIndex >= matches.size()) return false;
        const BindingMatch &match = matches.at(matchIndex);
        if (index < match.start || index >= match.end) return false;
    }
    return true;
}

bool isPlaceholderIndexToken(const QString &token)
{
    bool numeric = false;
    token.toInt(&numeric);
    if (numeric) return true;
    static const QSet<QString> numberWords{QStringLiteral("zero"), QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"), QStringLiteral("four"), QStringLiteral("five"), QStringLiteral("six"), QStringLiteral("seven"), QStringLiteral("eight"), QStringLiteral("nine")};
    return numberWords.contains(token);
}

QRegularExpression exactPlaceholderRegex()
{
    return QRegularExpression(QStringLiteral("(?<![A-Za-z0-9_])SPEECHER_BINDING_[0-9]+(?![A-Za-z0-9_])"));
}

} // namespace

QStringList BindingMatcher::normalizedTokens(const QString &text)
{
    QStringList values;
    const QList<Token> tokens = tokenizeWithSpans(text);
    values.reserve(tokens.size());
    for (const Token &token : tokens) values.append(token.text);
    return values;
}

QString BindingMatcher::normalizedPhrase(const QString &text)
{
    return normalizedTokens(text).join(QStringLiteral(" "));
}

QList<BindingMatch> BindingMatcher::findMatches(const QString &transcript, const QList<BindingRule> &rules)
{
    const QList<Token> transcriptTokens = tokenizeWithSpans(transcript);
    if (transcriptTokens.isEmpty() || rules.isEmpty()) return {};
    const QList<CompiledRule> compiled = compileRules(rules);
    QList<Candidate> candidates;
    for (int ruleIndex = 0; ruleIndex < compiled.size(); ++ruleIndex) {
        const CompiledRule &rule = compiled.at(ruleIndex);
        for (int start = 0; start + rule.tokens.size() <= transcriptTokens.size(); ++start) {
            if (sequenceMatches(transcriptTokens, start, rule.tokens)) candidates.append({ruleIndex, start, int(rule.tokens.size())});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&compiled](const Candidate &left, const Candidate &right) {
        if (left.tokenCount != right.tokenCount) return left.tokenCount > right.tokenCount;
        if (left.startToken != right.startToken) return left.startToken < right.startToken;
        return compiled.at(left.ruleIndex).order < compiled.at(right.ruleIndex).order;
    });
    QVector<bool> occupied(transcriptTokens.size(), false);
    QList<BindingMatch> accepted;
    for (const Candidate &candidate : candidates) {
        bool overlaps = false;
        for (int offset = 0; offset < candidate.tokenCount; ++offset) {
            if (occupied.at(candidate.startToken + offset)) { overlaps = true; break; }
        }
        if (overlaps) continue;
        for (int offset = 0; offset < candidate.tokenCount; ++offset) occupied[candidate.startToken + offset] = true;
        const Token &first = transcriptTokens.at(candidate.startToken);
        const Token &last = transcriptTokens.at(candidate.startToken + candidate.tokenCount - 1);
        accepted.append({compiled.at(candidate.ruleIndex).rule, candidate.startToken, candidate.tokenCount, first.start, last.end});
    }
    std::sort(accepted.begin(), accepted.end(), [](const BindingMatch &left, const BindingMatch &right) { return left.start < right.start; });
    return accepted;
}

BindingProcessingResult BindingMatcher::process(const QString &transcript, const QList<BindingRule> &rules)
{
    BindingProcessingResult result;
    result.boundText = transcript;
    result.placeholderText = transcript;
    const QList<BindingMatch> matches = findMatches(transcript, rules);
    if (matches.isEmpty()) return result;
    result.placeholders.reserve(matches.size());
    for (int index = 0; index < matches.size(); ++index) result.placeholders.append({QStringLiteral("SPEECHER_BINDING_%1").arg(index), matches.at(index).rule.replacement});
    result.boundText = replaceMatches(transcript, matches, [](int, const BindingMatch &match) { return match.rule.replacement; });
    result.placeholderText = replaceMatches(transcript, matches, [](int index, const BindingMatch &) { return QStringLiteral("SPEECHER_BINDING_%1").arg(index); });
    result.canSkipRefinement = allTranscriptWordsCovered(transcript, matches);
    return result;
}

QString BindingMatcher::applyOutsidePlaceholders(const QString &text, const QList<BindingRule> &rules)
{
    if (text.isEmpty() || rules.isEmpty()) return text;
    const QRegularExpression placeholderRegex = exactPlaceholderRegex();
    QRegularExpressionMatchIterator iterator = placeholderRegex.globalMatch(text);
    QString output;
    qsizetype cursor = 0;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const qsizetype start = match.capturedStart(0);
        const qsizetype end = match.capturedEnd(0);
        output += process(text.mid(cursor, start - cursor), rules).boundText;
        output += text.mid(start, end - start);
        cursor = end;
    }
    output += process(text.mid(cursor), rules).boundText;
    return output;
}

BindingRestoreResult BindingMatcher::restorePlaceholders(const QString &refinedText, const QList<BindingPlaceholder> &placeholders)
{
    if (placeholders.isEmpty()) return {true, refinedText};
    QHash<QString, QString> replacementByPlaceholder;
    for (const BindingPlaceholder &placeholder : placeholders) replacementByPlaceholder.insert(placeholder.placeholder, placeholder.replacement);
    const QRegularExpression exactPlaceholder = exactPlaceholderRegex();
    QRegularExpressionMatchIterator iterator = exactPlaceholder.globalMatch(refinedText);
    QString restored;
    QString residue;
    qsizetype cursor = 0;
    qsizetype residueCursor = 0;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString placeholder = match.captured(0);
        const auto replacement = replacementByPlaceholder.constFind(placeholder);
        if (replacement == replacementByPlaceholder.constEnd()) return {false, {}};
        const qsizetype start = match.capturedStart(0);
        const qsizetype end = match.capturedEnd(0);
        restored += refinedText.mid(cursor, start - cursor);
        restored += replacement.value();
        cursor = end;
        residue += refinedText.mid(residueCursor, start - residueCursor);
        residueCursor = end;
    }
    restored += refinedText.mid(cursor);
    residue += refinedText.mid(residueCursor);
    if (residue.contains(QStringLiteral("SPEECHER_BINDING"), Qt::CaseSensitive)) return {false, {}};
    const QStringList residueTokens = normalizedTokens(residue);
    for (int index = 0; index + 2 < residueTokens.size(); ++index) {
        if (residueTokens.at(index) == QStringLiteral("speecher") && residueTokens.at(index + 1) == QStringLiteral("binding") && isPlaceholderIndexToken(residueTokens.at(index + 2))) return {false, {}};
    }
    return {true, restored};
}

} // namespace speecher
