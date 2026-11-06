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

const QRegularExpression &exactPlaceholderRegex()
{
    static const QRegularExpression regex(QStringLiteral("(?<![A-Za-z0-9_])SPEECHER_BINDING_[0-9]+(?![A-Za-z0-9_])"));
    return regex;
}

// A placeholder the model reworded: any case, with or without separators,
// such as "speecherbinding0" or "Speecher Binding0".
const QRegularExpression &rewordedPlaceholderRegex()
{
    static const QRegularExpression regex(QStringLiteral("speecher[\\s_-]*binding[\\s_-]*[0-9]+"),
                                          QRegularExpression::CaseInsensitiveOption);
    return regex;
}

// The placeholder-like runs in text, exact or reworded, in lower case.
QSet<QString> rewordedPlaceholders(const QString &text)
{
    QSet<QString> found;
    QRegularExpressionMatchIterator iterator = rewordedPlaceholderRegex().globalMatch(text);
    while (iterator.hasNext()) found.insert(iterator.next().captured(0).toLower());
    return found;
}

// The index of each "speecher binding <index>" in text's words, such as
// "0" or "zero".
QSet<QString> spokenPlaceholderIndexes(const QString &text)
{
    const QStringList tokens = BindingMatcher::normalizedTokens(text);
    QSet<QString> indexes;
    for (qsizetype index = 2; index < tokens.size(); ++index) {
        if (tokens.at(index - 2) == QStringLiteral("speecher") && tokens.at(index - 1) == QStringLiteral("binding")
            && isPlaceholderIndexToken(tokens.at(index))) indexes.insert(tokens.at(index));
    }
    return indexes;
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
    const QRegularExpression &placeholderRegex = exactPlaceholderRegex();
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

// Placeholder-like text left once the known placeholders are restored is a
// lost or invented snippet, unless sourceText, what the user gave refinement
// before placeholders went in, holds the same text: a selected document can
// contain SPEECHER_BINDING_0, and a dictation can be about "speecher binding
// zero".
BindingRestoreResult BindingMatcher::restorePlaceholders(const QString &refinedText,
                                                         const QList<BindingPlaceholder> &placeholders,
                                                         const QString &sourceText)
{
    const QHash<QString, QString> replacementByPlaceholder = replacementsByPlaceholder(placeholders);
    const QRegularExpression &exactPlaceholder = exactPlaceholderRegex();
    QRegularExpressionMatchIterator iterator = exactPlaceholder.globalMatch(refinedText);
    QString restored;
    QString residue;
    qsizetype cursor = 0;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString placeholder = match.captured(0);
        const auto replacement = replacementByPlaceholder.constFind(placeholder);
        // One no binding produced stays as written, for the checks below.
        if (replacement == replacementByPlaceholder.constEnd()) continue;
        const qsizetype start = match.capturedStart(0);
        const qsizetype end = match.capturedEnd(0);
        restored += refinedText.mid(cursor, start - cursor);
        restored += replacement.value();
        residue += refinedText.mid(cursor, start - cursor);
        cursor = end;
    }
    restored += refinedText.mid(cursor);
    residue += refinedText.mid(cursor);
    const QString prefix = QStringLiteral("SPEECHER_BINDING");
    if (residue.contains(prefix, Qt::CaseSensitive) && !sourceText.contains(prefix, Qt::CaseSensitive)) return {false, {}};
    if (!rewordedPlaceholders(sourceText).contains(rewordedPlaceholders(residue))) return {false, {}};
    if (!spokenPlaceholderIndexes(sourceText).contains(spokenPlaceholderIndexes(residue))) return {false, {}};
    return {true, restored};
}

QHash<QString, QString> BindingMatcher::replacementsByPlaceholder(const QList<BindingPlaceholder> &placeholders)
{
    QHash<QString, QString> replacements;
    for (const BindingPlaceholder &placeholder : placeholders) replacements.insert(placeholder.placeholder, placeholder.replacement);
    return replacements;
}

// The placeholders that restorePlaceholders would restore are restored the
// same way; one it doesn't know stays as written, as a preview has no result
// to refuse. A placeholder still streaming in at the end, where more digits
// could follow, is hidden until the text moves past it.
QString BindingMatcher::restorePlaceholdersForPreview(const QString &streamedText, const QHash<QString, QString> &replacements)
{
    QString text = streamedText;
    const qsizetype tail = text.lastIndexOf(QStringLiteral("SPEECHER"));
    if (tail >= 0) {
        static const QRegularExpression placeholderAtTail(QStringLiteral("SPEECHER_BINDING_[0-9]+"));
        const QRegularExpressionMatch match = placeholderAtTail.match(text, tail, QRegularExpression::NormalMatch,
                                                                      QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch() || match.capturedEnd() == text.size()) text.truncate(tail);
    }
    // One pass, continuing after each splice, so a replacement is never
    // scanned for placeholders of its own.
    QString restored;
    qsizetype cursor = 0;
    QRegularExpressionMatchIterator iterator = exactPlaceholderRegex().globalMatch(text);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        restored += text.mid(cursor, match.capturedStart(0) - cursor);
        restored += replacements.value(match.captured(0), match.captured(0));
        cursor = match.capturedEnd(0);
    }
    restored += text.mid(cursor);
    return restored;
}

} // namespace speecher
