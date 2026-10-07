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
        if (result.size() >= maxKeyterms) {
            break;
        }
        const QString term = rawTerm.simplified();
        if (term.isEmpty() || seen.contains(term)) {
            continue;
        }
        const int termTokens = tokenCount(term);
        if (termTokens <= 0 || tokens + termTokens > maxTokens) {
            continue;
        }
        result << term;
        seen.insert(term);
        tokens += termTokens;
    }
    return result;
}

QStringList claudeVoiceKeyterms(const QStringList &terms)
{
    constexpr qsizetype maxHeaderBytes = 1024;
    QStringList kept;
    QSet<QByteArray> seen;
    qsizetype bytes = 0;
    for (const QString &value : terms) {
        const QString term = value.simplified();
        const QByteArray utf8 = term.toUtf8();
        const QByteArray key = utf8.toLower();
        if (term.isEmpty() || seen.contains(key)) {
            continue;
        }
        const qsizetype separator = kept.isEmpty() ? 0 : 1;
        if (bytes + separator + utf8.size() > maxHeaderBytes) {
            continue;
        }
        seen.insert(key);
        kept.append(term);
        bytes += separator + utf8.size();
    }
    return kept;
}

QStringList speechKeyterms(const QStringList &terms, const QString &speechProviderId)
{
    if (speechProviderId == QStringLiteral("claude")) {
        return claudeVoiceKeyterms(limited(terms));
    }
    if (speechProviderId == QStringLiteral("endpoint")) {
        return limited(terms);
    }
    return {};
}

QString summary(const QStringList &terms, const QString &speechProviderId)
{
    return summary(terms, terms, speechProviderId);
}

QString summary(const QStringList &terms, const QStringList &keyTerms, const QString &speechProviderId)
{
    // `terms` is the whole stored list. Saying how many of them each consumer
    // actually receives is the point of the row, so the over-cap sentences
    // name the sent counts rather than pretending the rest are gone.
    const QString refinement = terms.size() > maxRefinementTerms
        ? QStringLiteral("the first %1 are used for refinement").arg(maxRefinementTerms)
        : QStringLiteral("all are used for refinement");
    if (speechProviderId != QStringLiteral("claude") && speechProviderId != QStringLiteral("endpoint")) {
        return (terms.size() == 1 ? QStringLiteral("1 term, used for refinement")
                                  : QStringLiteral("%1 terms, %2").arg(terms.size()).arg(refinement));
    }
    const QStringList sent = speechKeyterms(keyTerms, speechProviderId);
    if (sent.size() < terms.size()) {
        const QString count = terms.size() == 1 ? QStringLiteral("1 term") : QStringLiteral("%1 terms").arg(terms.size());
        const QString sentCount =
            sent.size() == 1 ? QStringLiteral("1 is a key term") : QStringLiteral("%1 are key terms").arg(sent.size());
        return QStringLiteral("%1. %2, and %3.").arg(count, sentCount, refinement);
    }
    return QStringLiteral("%1 of %2 key terms").arg(terms.size()).arg(maxKeyterms);
}

} // namespace speecher::VocabularyLimit
