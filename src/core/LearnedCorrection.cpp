#include "core/LearnedCorrection.h"

#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace speecher {
namespace {

QString comparableText(const QString &text)
{
    QString result;
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            result.append(character.toCaseFolded());
        }
    }
    return result;
}

QString comparableCaseSpacingAndHyphens(const QString &text)
{
    QString result;
    for (const QChar character : text) {
        if (!character.isSpace() && character != QLatin1Char('-')) {
            result.append(character.toCaseFolded());
        }
    }
    return result;
}

QStringList words(const QString &text)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}]+"));
    QStringList result;
    auto matches = word.globalMatch(text);
    while (matches.hasNext()) {
        result.append(matches.next().captured().toCaseFolded());
    }
    return result;
}

bool looksSecret(const QString &text)
{
    static const QRegularExpression secret(
        QStringLiteral("(?:https?://|[\\w.+-]+@[\\w.-]+\\.[A-Za-z]{2,}|"
                       "(?:password|secret|token|api[ _-]?key)\\s*[:=]|"
                       "\\b\\d{12,19}\\b|\\b[A-Za-z0-9_/-]{20,}={0,2}\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression separators(QStringLiteral("[\\s-]"));
    static const QRegularExpression cardDigits(QStringLiteral("(?<!\\d)\\d{12,19}(?!\\d)"));
    QString compact = text;
    compact.remove(separators);
    return secret.match(text).hasMatch() || cardDigits.match(compact).hasMatch();
}

bool isSubsequence(const QString &shorter, const QString &longer)
{
    int index = 0;
    for (const QChar character : longer) {
        if (index < shorter.size() && shorter.at(index) == character) ++index;
    }
    return index == shorter.size();
}

int editDistance(const QString &left, const QString &right)
{
    QVector<int> previous(right.size() + 1);
    QVector<int> current(right.size() + 1);
    for (int index = 0; index <= right.size(); ++index) {
        previous[index] = index;
    }
    for (int leftIndex = 1; leftIndex <= left.size(); ++leftIndex) {
        current[0] = leftIndex;
        for (int rightIndex = 1; rightIndex <= right.size(); ++rightIndex) {
            current[rightIndex] = std::min({
                previous[rightIndex] + 1,
                current[rightIndex - 1] + 1,
                previous[rightIndex - 1]
                    + (left.at(leftIndex - 1).toCaseFolded()
                               == right.at(rightIndex - 1).toCaseFolded()
                           ? 0
                           : 1),
            });
        }
        previous.swap(current);
    }
    return previous.last();
}

} // namespace

std::optional<CorrectionEvidence> analyzeCorrection(const QString &inserted,
                                                    const QString &edited)
{
    if (inserted == edited || inserted.isEmpty() || edited.isEmpty()
        || looksSecret(inserted) || looksSecret(edited)) {
        return std::nullopt;
    }

    int prefix = 0;
    const int sharedLength = std::min(inserted.size(), edited.size());
    while (prefix < sharedLength && inserted.at(prefix) == edited.at(prefix)) {
        ++prefix;
    }
    int suffix = 0;
    while (suffix < inserted.size() - prefix
           && suffix < edited.size() - prefix
           && inserted.at(inserted.size() - suffix - 1)
               == edited.at(edited.size() - suffix - 1)) {
        ++suffix;
    }

    while (prefix > 0 && inserted.at(prefix - 1).isLetterOrNumber()) {
        --prefix;
    }
    int insertedEnd = inserted.size() - suffix;
    int editedEnd = edited.size() - suffix;
    while (insertedEnd < inserted.size() && editedEnd < edited.size()
           && inserted.at(insertedEnd).isLetterOrNumber()
           && edited.at(editedEnd).isLetterOrNumber()) {
        ++insertedEnd;
        ++editedEnd;
    }

    const QString original = inserted.mid(prefix, insertedEnd - prefix).trimmed();
    const QString corrected = edited.mid(prefix, editedEnd - prefix).trimmed();
    if (original.isEmpty() || corrected.isEmpty() || original == corrected
        || original.size() > 64 || corrected.size() > 64
        || looksSecret(original) || looksSecret(corrected)) {
        return std::nullopt;
    }

    const QString comparableOriginal = comparableText(original);
    const QString comparableCorrected = comparableText(corrected);
    if (comparableOriginal.isEmpty() || comparableCorrected.isEmpty()) {
        return std::nullopt;
    }
    if (comparableOriginal == comparableCorrected) {
        if (comparableCaseSpacingAndHyphens(original)
            != comparableCaseSpacingAndHyphens(corrected)) {
            return std::nullopt;
        }
        return CorrectionEvidence{original, corrected, 0.98};
    }
    if ((comparableOriginal.size() < comparableCorrected.size()
         && isSubsequence(comparableOriginal, comparableCorrected))
        || (comparableCorrected.size() < comparableOriginal.size()
            && isSubsequence(comparableCorrected, comparableOriginal))) {
        return std::nullopt;
    }

    const QStringList originalWords = words(original);
    const QStringList correctedWords = words(corrected);
    if (originalWords.size() > 3 || correctedWords.size() > 3) {
        return std::nullopt;
    }
    for (const QString &word : originalWords) {
        if (word.size() > 1 && correctedWords.contains(word)) {
            return std::nullopt;
        }
    }
    if (originalWords.size() == correctedWords.size() && originalWords.size() > 1) {
        int changedWords = 0;
        for (int index = 0; index < originalWords.size(); ++index) {
            changedWords += originalWords.at(index) != correctedWords.at(index);
        }
        if (changedWords > 1) {
            return std::nullopt;
        }
    }

    const int longest = std::max(comparableOriginal.size(), comparableCorrected.size());
    const int shortest = std::min(comparableOriginal.size(), comparableCorrected.size());
    if (shortest * 3 < longest
        || editDistance(comparableOriginal, comparableCorrected)
            > std::max(3, (longest + 1) / 2)) {
        return std::nullopt;
    }
    return CorrectionEvidence{original, corrected, 0.75};
}

} // namespace speecher
