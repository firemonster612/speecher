#include "core/VocabularyLimit.h"

#include <QRegularExpression>
#include <QSet>

namespace speecher::VocabularyLimit {

int tokenCount(const QString &term)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return term.trimmed().split(whitespace, Qt::SkipEmptyParts).size();
}

int tokenCount(const QStringList &terms)
{
    int count = 0;
    for (const QString &term : terms) {
        count += tokenCount(term);
    }
    return count;
}

QStringList limited(const QStringList &terms)
{
    QStringList result;
    QSet<QString> seen;
    int tokens = 0;
    for (const QString &rawTerm : terms) {
        const QString term = rawTerm.simplified();
        if (term.isEmpty() || seen.contains(term)) {
            continue;
        }
        const int termTokens = tokenCount(term);
        if (termTokens <= 0 || result.size() >= maxKeyterms || tokens + termTokens > maxTokens) {
            continue;
        }
        result << term;
        seen.insert(term);
        tokens += termTokens;
    }
    return result;
}

QString summary(const QStringList &terms, bool speechTakesHints)
{
    // `terms` is the whole stored list. Saying how many of them each consumer
    // actually receives is the point of the row, so the over-cap sentences
    // name the sent counts rather than pretending the rest are gone.
    const QString refinement = terms.size() > maxRefinementTerms
        ? QStringLiteral("the first %1 are used for refinement").arg(maxRefinementTerms)
        : QStringLiteral("all are used for refinement");
    if (!speechTakesHints) {
        return (terms.size() == 1 ? QStringLiteral("1 term, used for refinement")
                                  : QStringLiteral("%1 terms, %2").arg(terms.size()).arg(refinement));
    }
    const QStringList sent = limited(terms);
    if (sent.size() < terms.size()) {
        return QStringLiteral("%1 terms. The first %2 are speech hints, and %3.")
            .arg(terms.size())
            .arg(sent.size())
            .arg(refinement);
    }
    return QStringLiteral("%1 of %2 speech hints, using %3 of %4 tokens")
        .arg(terms.size())
        .arg(maxKeyterms)
        .arg(tokenCount(terms))
        .arg(maxTokens);
}

} // namespace speecher::VocabularyLimit
