#include "core/bindings/NoBindDirectiveParser.h"

#include "core/bindings/BindingMatcher.h"

#include <QSet>
#include <limits>

namespace speecher {
namespace {

struct Token {
    QString text;
    qsizetype start = 0;
    qsizetype end = 0;
};

struct DirectiveSpan {
    int startToken = 0;
    int endToken = 0;
    qsizetype start = 0;
    qsizetype end = 0;
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

bool isNoBindNegationAt(const QList<Token> &tokens, int index, int *tokenCount)
{
    const QString token = tokens.at(index).text;
    if (token == QStringLiteral("not") || token == QStringLiteral("never") || token == QStringLiteral("dont")) {
        *tokenCount = 1;
        return true;
    }
    if (token == QStringLiteral("don") && index + 1 < tokens.size() && tokens.at(index + 1).text == QStringLiteral("t")) {
        *tokenCount = 2;
        return true;
    }
    if (token == QStringLiteral("do") && index + 1 < tokens.size() && tokens.at(index + 1).text == QStringLiteral("not")) {
        *tokenCount = 2;
        return true;
    }
    return false;
}

bool findBindingAction(const QList<Token> &tokens, int start, int *actionEndToken)
{
    const int end = qMin(tokens.size(), start + 12);
    for (int index = start; index < end; ++index) {
        const QString token = tokens.at(index).text;
        if (token == QStringLiteral("bind") || token == QStringLiteral("binding") || token == QStringLiteral("bindings") || token == QStringLiteral("replace") || token == QStringLiteral("replaces") || token == QStringLiteral("replacing") || token == QStringLiteral("replacement")) {
            *actionEndToken = index;
            return true;
        }
        if (token != QStringLiteral("turn")) continue;
        int intoIndex = -1;
        for (int intoCandidate = index + 1; intoCandidate < qMin(tokens.size(), index + 8); ++intoCandidate) {
            if (tokens.at(intoCandidate).text == QStringLiteral("into")) { intoIndex = intoCandidate; break; }
        }
        if (intoIndex < 0) continue;
        for (int bindingCandidate = intoIndex + 1; bindingCandidate < qMin(tokens.size(), intoIndex + 5); ++bindingCandidate) {
            const QString bindingToken = tokens.at(bindingCandidate).text;
            if (bindingToken == QStringLiteral("binding") || bindingToken == QStringLiteral("bindings")) {
                *actionEndToken = bindingCandidate;
                return true;
            }
        }
    }
    return false;
}

QList<DirectiveSpan> findNoBindDirectiveSpans(const QString &text)
{
    const QList<Token> tokens = tokenizeWithSpans(text);
    QList<DirectiveSpan> spans;
    if (tokens.isEmpty()) return spans;
    for (int index = 0; index < tokens.size(); ++index) {
        int negationTokenCount = 0;
        if (!isNoBindNegationAt(tokens, index, &negationTokenCount)) continue;
        int actionEndToken = -1;
        if (!findBindingAction(tokens, index + negationTokenCount, &actionEndToken)) continue;
        spans.append({index, actionEndToken + 1, tokens.at(index).start, tokens.at(actionEndToken).end});
    }
    return spans;
}

void appendNoBindPhrase(QStringList *phrases, QSet<QString> *seen, const BindingRule &rule)
{
    const QString normalized = BindingMatcher::normalizedPhrase(rule.phrase);
    if (!normalized.isEmpty() && !seen->contains(normalized)) {
        seen->insert(normalized);
        phrases->append(normalized);
    }
}

bool matchIsInsideDirective(const BindingMatch &match, const DirectiveSpan &directive)
{
    const int matchEndToken = match.startToken + match.tokenCount;
    return match.startToken >= directive.startToken && matchEndToken <= directive.endToken;
}

const BindingMatch *nearestMatchBeforeDirective(const QList<BindingMatch> &matches, const DirectiveSpan &directive)
{
    const BindingMatch *nearest = nullptr;
    int nearestDistance = std::numeric_limits<int>::max();
    for (const BindingMatch &match : matches) {
        const int matchEndToken = match.startToken + match.tokenCount;
        if (matchEndToken > directive.startToken) continue;
        const int distance = directive.startToken - matchEndToken;
        if (distance <= 8 && distance < nearestDistance) {
            nearest = &match;
            nearestDistance = distance;
        }
    }
    return nearest;
}

} // namespace

bool NoBindDirectiveParser::hasDirective(const QString &transcript)
{
    return !findNoBindDirectiveSpans(transcript).isEmpty();
}

QStringList NoBindDirectiveParser::excludedPhrases(const QString &transcript, const QList<BindingRule> &rules)
{
    QStringList phrases;
    QSet<QString> seen;
    const QList<DirectiveSpan> directives = findNoBindDirectiveSpans(transcript);
    if (directives.isEmpty()) return phrases;
    const QList<BindingMatch> matches = BindingMatcher::findMatches(transcript, rules);
    for (const DirectiveSpan &directive : directives) {
        bool matchedInsideDirective = false;
        for (const BindingMatch &match : matches) {
            if (!matchIsInsideDirective(match, directive)) continue;
            appendNoBindPhrase(&phrases, &seen, match.rule);
            matchedInsideDirective = true;
        }
        if (!matchedInsideDirective) {
            if (const BindingMatch *nearest = nearestMatchBeforeDirective(matches, directive)) appendNoBindPhrase(&phrases, &seen, nearest->rule);
        }
    }
    return phrases;
}

} // namespace speecher
