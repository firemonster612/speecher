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

bool takesKeyterms(const QString &speechProviderId)
{
    return speechProviderId == QStringLiteral("claude") || speechProviderId == QStringLiteral("endpoint");
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

} // namespace speecher::VocabularyLimit
