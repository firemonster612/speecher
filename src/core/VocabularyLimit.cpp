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

QString summary(const QStringList &terms)
{
    // The service caps both the number of terms and their total size; say so
    // as a sentence rather than as two fractions.
    return QStringLiteral("%1 of %2 terms, using %3 of %4 tokens")
        .arg(terms.size())
        .arg(maxKeyterms)
        .arg(tokenCount(terms))
        .arg(maxTokens);
}

} // namespace speecher::VocabularyLimit
