#include "core/BindingProcessor.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <functional>
#include <limits>

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

struct AcceptedMatch {
    BindingRule rule;
    int startToken = 0;
    int tokenCount = 0;
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
        if (current.isEmpty()) {
            return;
        }
        tokens.append({current.toCaseFolded(), tokenStart, end});
        current.clear();
        tokenStart = -1;
    };

    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch.isLetterOrNumber()) {
            if (current.isEmpty()) {
                tokenStart = index;
            }
            current.append(ch);
        } else {
            flush(index);
        }
    }
    flush(text.size());
    return tokens;
}

QStringList tokenTexts(const QList<Token> &tokens)
{
    QStringList values;
    values.reserve(tokens.size());
    for (const Token &token : tokens) {
        values.append(token.text);
    }
    return values;
}

QList<CompiledRule> compileRules(const QList<BindingRule> &rules)
{
    QList<CompiledRule> compiled;
    QSet<QString> seen;

    for (int index = 0; index < rules.size(); ++index) {
        const BindingRule &rule = rules.at(index);
        const QString phrase = rule.phrase.trimmed();
        const QStringList tokens = BindingProcessor::normalizedTokens(phrase);
        const QString normalized = tokens.join(QStringLiteral(" "));
        if (phrase.isEmpty() || rule.replacement.trimmed().isEmpty() || tokens.isEmpty() || seen.contains(normalized)) {
            continue;
        }
        seen.insert(normalized);
        compiled.append({BindingRule{phrase, rule.replacement}, tokens, index});
    }

    return compiled;
}

bool sequenceMatches(const QList<Token> &transcriptTokens,
                     int start,
                     const QStringList &ruleTokens)
{
    if (start + ruleTokens.size() > transcriptTokens.size()) {
        return false;
    }

    for (int offset = 0; offset < ruleTokens.size(); ++offset) {
        if (transcriptTokens.at(start + offset).text != ruleTokens.at(offset)) {
            return false;
        }
    }
    return true;
}

QList<AcceptedMatch> findMatches(const QString &transcript, const QList<BindingRule> &rules)
{
    const QList<Token> transcriptTokens = tokenizeWithSpans(transcript);
    if (transcriptTokens.isEmpty() || rules.isEmpty()) {
        return {};
    }

    const QList<CompiledRule> compiled = compileRules(rules);
    QList<Candidate> candidates;
    for (int ruleIndex = 0; ruleIndex < compiled.size(); ++ruleIndex) {
        const CompiledRule &rule = compiled.at(ruleIndex);
        for (int start = 0; start + rule.tokens.size() <= transcriptTokens.size(); ++start) {
            if (sequenceMatches(transcriptTokens, start, rule.tokens)) {
                candidates.append({ruleIndex, start, int(rule.tokens.size())});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [&compiled](const Candidate &left, const Candidate &right) {
        if (left.tokenCount != right.tokenCount) {
            return left.tokenCount > right.tokenCount;
        }
        if (left.startToken != right.startToken) {
            return left.startToken < right.startToken;
        }
        return compiled.at(left.ruleIndex).order < compiled.at(right.ruleIndex).order;
    });

    QVector<bool> occupied(transcriptTokens.size(), false);
    QList<AcceptedMatch> accepted;
    for (const Candidate &candidate : candidates) {
        bool overlaps = false;
        for (int offset = 0; offset < candidate.tokenCount; ++offset) {
            if (occupied.at(candidate.startToken + offset)) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) {
            continue;
        }

        for (int offset = 0; offset < candidate.tokenCount; ++offset) {
            occupied[candidate.startToken + offset] = true;
        }
        const Token &first = transcriptTokens.at(candidate.startToken);
        const Token &last = transcriptTokens.at(candidate.startToken + candidate.tokenCount - 1);
        accepted.append({compiled.at(candidate.ruleIndex).rule,
                         candidate.startToken,
                         candidate.tokenCount,
                         first.start,
                         last.end});
    }

    std::sort(accepted.begin(), accepted.end(), [](const AcceptedMatch &left, const AcceptedMatch &right) {
        return left.start < right.start;
    });
    return accepted;
}

QString replaceMatches(const QString &transcript,
                       const QList<AcceptedMatch> &matches,
                       const std::function<QString(int, const AcceptedMatch &)> &replacement)
{
    QString output;
    qsizetype cursor = 0;
    for (int index = 0; index < matches.size(); ++index) {
        const AcceptedMatch &match = matches.at(index);
        output += transcript.mid(cursor, match.start - cursor);
        output += replacement(index, match);
        cursor = match.end;
    }
    output += transcript.mid(cursor);
    return output;
}

bool allTranscriptWordsCovered(const QString &transcript, const QList<AcceptedMatch> &matches)
{
    if (matches.isEmpty()) {
        return false;
    }

    int matchIndex = 0;
    for (qsizetype index = 0; index < transcript.size(); ++index) {
        if (!transcript.at(index).isLetterOrNumber()) {
            continue;
        }
        while (matchIndex < matches.size() && matches.at(matchIndex).end <= index) {
            ++matchIndex;
        }
        if (matchIndex >= matches.size()) {
            return false;
        }
        const AcceptedMatch &match = matches.at(matchIndex);
        if (index < match.start || index >= match.end) {
            return false;
        }
    }
    return true;
}

bool isPlaceholderIndexToken(const QString &token)
{
    bool numeric = false;
    token.toInt(&numeric);
    if (numeric) {
        return true;
    }

    static const QSet<QString> numberWords{
        QStringLiteral("zero"),
        QStringLiteral("one"),
        QStringLiteral("two"),
        QStringLiteral("three"),
        QStringLiteral("four"),
        QStringLiteral("five"),
        QStringLiteral("six"),
        QStringLiteral("seven"),
        QStringLiteral("eight"),
        QStringLiteral("nine"),
    };
    return numberWords.contains(token);
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
        if (token == QStringLiteral("bind") || token == QStringLiteral("binding")
            || token == QStringLiteral("bindings") || token == QStringLiteral("replace")
            || token == QStringLiteral("replaces") || token == QStringLiteral("replacing")
            || token == QStringLiteral("replacement")) {
            *actionEndToken = index;
            return true;
        }
        if (token != QStringLiteral("turn")) {
            continue;
        }

        int intoIndex = -1;
        for (int intoCandidate = index + 1; intoCandidate < qMin(tokens.size(), index + 8); ++intoCandidate) {
            if (tokens.at(intoCandidate).text == QStringLiteral("into")) {
                intoIndex = intoCandidate;
                break;
            }
        }
        if (intoIndex < 0) {
            continue;
        }

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
    if (tokens.isEmpty()) {
        return spans;
    }

    for (int index = 0; index < tokens.size(); ++index) {
        int negationTokenCount = 0;
        if (!isNoBindNegationAt(tokens, index, &negationTokenCount)) {
            continue;
        }

        int actionEndToken = -1;
        if (!findBindingAction(tokens, index + negationTokenCount, &actionEndToken)) {
            continue;
        }

        spans.append({index, actionEndToken + 1, tokens.at(index).start, tokens.at(actionEndToken).end});
    }

    return spans;
}

void appendNoBindPhrase(QStringList *phrases, QSet<QString> *seen, const BindingRule &rule)
{
    const QString normalized = BindingProcessor::normalizedPhrase(rule.phrase);
    if (!normalized.isEmpty() && !seen->contains(normalized)) {
        seen->insert(normalized);
        phrases->append(normalized);
    }
}

bool matchIsInsideDirective(const AcceptedMatch &match, const DirectiveSpan &directive)
{
    const int matchEndToken = match.startToken + match.tokenCount;
    return match.startToken >= directive.startToken && matchEndToken <= directive.endToken;
}

const AcceptedMatch *nearestMatchBeforeDirective(const QList<AcceptedMatch> &matches, const DirectiveSpan &directive)
{
    const AcceptedMatch *nearest = nullptr;
    int nearestDistance = std::numeric_limits<int>::max();

    for (const AcceptedMatch &match : matches) {
        const int matchEndToken = match.startToken + match.tokenCount;
        if (matchEndToken > directive.startToken) {
            continue;
        }

        const int distance = directive.startToken - matchEndToken;
        if (distance <= 8 && distance < nearestDistance) {
            nearest = &match;
            nearestDistance = distance;
        }
    }

    return nearest;
}

bool containsExplicitNoBindDirective(const QString &text)
{
    return !findNoBindDirectiveSpans(text).isEmpty();
}

} // namespace

bool BindingProcessor::hasExplicitNoBindDirective(const QString &transcript)
{
    return containsExplicitNoBindDirective(transcript);
}

namespace {

QString validationMessage(BindingValidationIssue::Type type, int row, int duplicateOf = -1)
{
    const int displayRow = row + 1;
    switch (type) {
    case BindingValidationIssue::Type::EmptyPhrase:
        return QStringLiteral("Row %1 needs a spoken phrase containing at least one letter or number.").arg(displayRow);
    case BindingValidationIssue::Type::EmptyReplacement:
        return QStringLiteral("Row %1 needs replacement text.").arg(displayRow);
    case BindingValidationIssue::Type::DuplicatePhrase:
        return QStringLiteral("Row %1 duplicates the normalized spoken phrase from row %2.")
            .arg(displayRow)
            .arg(duplicateOf + 1);
    }
    return {};
}

QRegularExpression exactPlaceholderRegex()
{
    return QRegularExpression(QStringLiteral("(?<![A-Za-z0-9_])SPEECHER_BINDING_[0-9]+(?![A-Za-z0-9_])"));
}

} // namespace

QStringList BindingValidationResult::messages() const
{
    QStringList values;
    values.reserve(issues.size());
    for (const BindingValidationIssue &issue : issues) {
        values.append(issue.message);
    }
    return values;
}

QStringList BindingProcessor::normalizedTokens(const QString &text)
{
    return tokenTexts(tokenizeWithSpans(text));
}

QString BindingProcessor::normalizedPhrase(const QString &text)
{
    return normalizedTokens(text).join(QStringLiteral(" "));
}

BindingValidationResult BindingProcessor::validateRules(const QList<BindingRule> &rules)
{
    BindingValidationResult result;
    QHash<QString, int> seen;

    for (int row = 0; row < rules.size(); ++row) {
        const BindingRule &rule = rules.at(row);
        const QString phrase = rule.phrase.trimmed();
        const QStringList tokens = normalizedTokens(phrase);
        const QString normalized = tokens.join(QStringLiteral(" "));

        if (phrase.isEmpty() || tokens.isEmpty()) {
            result.issues.append({BindingValidationIssue::Type::EmptyPhrase,
                                  row,
                                  -1,
                                  validationMessage(BindingValidationIssue::Type::EmptyPhrase, row)});
            continue;
        }
        if (rule.replacement.trimmed().isEmpty()) {
            result.issues.append({BindingValidationIssue::Type::EmptyReplacement,
                                  row,
                                  -1,
                                  validationMessage(BindingValidationIssue::Type::EmptyReplacement, row)});
            continue;
        }
        if (seen.contains(normalized)) {
            const int duplicateOf = seen.value(normalized);
            result.issues.append({BindingValidationIssue::Type::DuplicatePhrase,
                                  row,
                                  duplicateOf,
                                  validationMessage(BindingValidationIssue::Type::DuplicatePhrase, row, duplicateOf)});
            continue;
        }

        seen.insert(normalized, row);
        result.rules.append({phrase, rule.replacement});
    }

    return result;
}

QStringList BindingProcessor::refinementVocabulary(const QList<BindingRule> &rules)
{
    QStringList vocabulary;
    QSet<QString> seen;
    const QList<BindingRule> validRules = validateRules(rules).rules;

    for (const BindingRule &rule : validRules) {
        const QString phrase = rule.phrase.trimmed();
        const QString normalized = normalizedPhrase(phrase);
        for (const QString &candidate : {phrase, normalized}) {
            const QString term = candidate.simplified();
            const QString key = term.toCaseFolded();
            if (!term.isEmpty() && !seen.contains(key)) {
                seen.insert(key);
                vocabulary.append(term);
            }
        }
    }

    return vocabulary;
}

QStringList BindingProcessor::explicitNoBindPhrases(const QString &transcript, const QList<BindingRule> &rules)
{
    QStringList phrases;
    QSet<QString> seen;

    const QList<DirectiveSpan> directives = findNoBindDirectiveSpans(transcript);
    if (directives.isEmpty()) {
        return phrases;
    }

    const QList<AcceptedMatch> matches = findMatches(transcript, validateRules(rules).rules);
    for (const DirectiveSpan &directive : directives) {
        bool matchedInsideDirective = false;
        for (const AcceptedMatch &match : matches) {
            if (!matchIsInsideDirective(match, directive)) {
                continue;
            }
            appendNoBindPhrase(&phrases, &seen, match.rule);
            matchedInsideDirective = true;
        }

        if (!matchedInsideDirective) {
            if (const AcceptedMatch *nearest = nearestMatchBeforeDirective(matches, directive)) {
                appendNoBindPhrase(&phrases, &seen, nearest->rule);
            }
        }
    }

    return phrases;
}

BindingProcessingResult BindingProcessor::process(const QString &transcript, const QList<BindingRule> &rules)
{
    BindingProcessingResult result;
    result.boundText = transcript;
    result.placeholderText = transcript;

    const QList<AcceptedMatch> matches = findMatches(transcript, rules);
    if (matches.isEmpty()) {
        return result;
    }

    result.placeholders.reserve(matches.size());
    for (int index = 0; index < matches.size(); ++index) {
        result.placeholders.append({QStringLiteral("SPEECHER_BINDING_%1").arg(index),
                                    matches.at(index).rule.replacement});
    }

    result.boundText = replaceMatches(transcript, matches, [](int, const AcceptedMatch &match) {
        return match.rule.replacement;
    });
    result.placeholderText = replaceMatches(transcript, matches, [](int index, const AcceptedMatch &) {
        return QStringLiteral("SPEECHER_BINDING_%1").arg(index);
    });
    result.canSkipRefinement = allTranscriptWordsCovered(transcript, matches);
    return result;
}

QString BindingProcessor::applyBindingsOutsidePlaceholders(const QString &text, const QList<BindingRule> &rules)
{
    if (text.isEmpty() || rules.isEmpty()) {
        return text;
    }

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

BindingRestoreResult BindingProcessor::restorePlaceholders(const QString &refinedText,
                                                           const QList<BindingPlaceholder> &placeholders)
{
    if (placeholders.isEmpty()) {
        return {true, refinedText};
    }

    QHash<QString, QString> replacementByPlaceholder;
    for (const BindingPlaceholder &placeholder : placeholders) {
        replacementByPlaceholder.insert(placeholder.placeholder, placeholder.replacement);
    }

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
        if (replacement == replacementByPlaceholder.constEnd()) {
            return {false, {}};
        }

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

    if (residue.contains(QStringLiteral("SPEECHER_BINDING"), Qt::CaseSensitive)) {
        return {false, {}};
    }

    const QStringList residueTokens = normalizedTokens(residue);
    for (int index = 0; index + 2 < residueTokens.size(); ++index) {
        if (residueTokens.at(index) == QStringLiteral("speecher")
            && residueTokens.at(index + 1) == QStringLiteral("binding")
            && isPlaceholderIndexToken(residueTokens.at(index + 2))) {
            return {false, {}};
        }
    }

    return {true, restored};
}

} // namespace speecher
