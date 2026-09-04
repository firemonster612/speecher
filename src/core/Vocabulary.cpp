#include "core/Vocabulary.h"

#include "core/VocabularyLimit.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTextBoundaryFinder>

#include <algorithm>

namespace speecher {
namespace {

QList<QStringList> csvRows(const QString &text, QString *error)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar character = text.at(index);
        if (quoted) {
            if (character == QLatin1Char('"')) {
                if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('"')) {
                    field.append(character);
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                field.append(character);
            }
            continue;
        }
        if (character == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
        } else if (character == QLatin1Char(',')) {
            row.append(field);
            field.clear();
        } else if (character == QLatin1Char('\n')) {
            row.append(field);
            field.clear();
            if (!row.join(QString()).trimmed().isEmpty()) {
                rows.append(row);
            }
            row.clear();
        } else if (character != QLatin1Char('\r')) {
            field.append(character);
        }
    }
    if (quoted) {
        if (error) {
            *error = QStringLiteral("CSV contains an unterminated quoted field.");
        }
        return {};
    }
    row.append(field);
    if (!row.join(QString()).trimmed().isEmpty()) {
        rows.append(row);
    }
    return rows;
}

int column(const QStringList &header, const QString &name, int fallback = -1)
{
    const int index = header.indexOf(name);
    return index >= 0 ? index : fallback;
}

QString fieldAt(const QStringList &row, int index)
{
    return index >= 0 && index < row.size() ? row.at(index).trimmed() : QString();
}

} // namespace

bool containsVocabularyTerm(const QString &text, const QString &term)
{
    const QStringList words = term.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return false;
    }

    QStringList escapedWords;
    escapedWords.reserve(words.size());
    for (const QString &word : words) {
        escapedWords.append(QRegularExpression::escape(word));
    }
    const QRegularExpression expression(
        escapedWords.join(QStringLiteral("\\s+")),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::UseUnicodePropertiesOption);
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Word, text);
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        boundaries.setPosition(match.capturedStart());
        const bool startsAtBoundary = boundaries.isAtBoundary();
        boundaries.setPosition(match.capturedEnd());
        if (startsAtBoundary && boundaries.isAtBoundary()) {
            return true;
        }
    }
    return false;
}

QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries)
{
    QList<VocabularyEntry> normalized;
    for (VocabularyEntry entry : entries) {
        entry.term = entry.term.simplified();
        entry.source = entry.source.simplified();
        if (entry.source.isEmpty()) {
            entry.source = QStringLiteral("manual");
        }
        entry.frequency = qMax(0, entry.frequency);
        entry.lastUsedMs = qMax<qint64>(0, entry.lastUsedMs);
        if (entry.term.isEmpty()) {
            continue;
        }
        auto duplicate = std::find_if(normalized.begin(), normalized.end(), [&entry](const VocabularyEntry &existing) {
            return existing.term.compare(entry.term, Qt::CaseInsensitive) == 0;
        });
        if (duplicate == normalized.end()) {
            normalized.append(entry);
        } else {
            duplicate->starred = duplicate->starred || entry.starred;
            duplicate->frequency = qMax(duplicate->frequency, entry.frequency);
            duplicate->lastUsedMs = qMax(duplicate->lastUsedMs, entry.lastUsedMs);
        }
    }
    std::sort(normalized.begin(), normalized.end(), [](const VocabularyEntry &left, const VocabularyEntry &right) {
        if (left.starred != right.starred) {
            return left.starred;
        }
        if (left.frequency != right.frequency) {
            return left.frequency > right.frequency;
        }
        if (left.lastUsedMs != right.lastUsedMs) {
            return left.lastUsedMs > right.lastUsedMs;
        }
        return left.term.toCaseFolded() < right.term.toCaseFolded();
    });

    QList<VocabularyEntry> limited;
    QStringList terms;
    for (const VocabularyEntry &entry : normalized) {
        const QStringList candidate = terms + QStringList{entry.term};
        if (candidate.size() > VocabularyLimit::maxKeyterms
            || VocabularyLimit::tokenCount(candidate) > VocabularyLimit::maxTokens) {
            continue;
        }
        terms.append(entry.term);
        limited.append(entry);
    }
    return limited;
}

QList<VocabularyEntry> parseVocabularyCsv(const QByteArray &csv, QString *error)
{
    if (error) {
        error->clear();
    }
    const QList<QStringList> rows = csvRows(QString::fromUtf8(csv), error);
    if (rows.isEmpty()) {
        return {};
    }

    QStringList header;
    for (const QString &field : rows.first()) {
        header.append(field.trimmed().toCaseFolded().replace(QLatin1Char(' '), QLatin1Char('_')));
    }
    const bool hasHeader = header.contains(QStringLiteral("term"));
    const int termColumn = hasHeader ? column(header, QStringLiteral("term")) : 0;
    const int sourceColumn = hasHeader ? column(header, QStringLiteral("source")) : 1;
    const int starredColumn = hasHeader ? column(header, QStringLiteral("starred")) : 2;
    const int frequencyColumn = hasHeader ? column(header, QStringLiteral("frequency")) : 3;
    const int lastUsedColumn = hasHeader
        ? column(header, QStringLiteral("last_used_ms"), column(header, QStringLiteral("last_used")))
        : 4;

    QList<VocabularyEntry> entries;
    for (int index = hasHeader ? 1 : 0; index < rows.size(); ++index) {
        const QStringList &row = rows.at(index);
        VocabularyEntry entry;
        entry.term = fieldAt(row, termColumn);
        entry.source = fieldAt(row, sourceColumn);
        if (entry.source.isEmpty()) {
            entry.source = QStringLiteral("csv");
        }
        const QString starred = fieldAt(row, starredColumn).toCaseFolded();
        entry.starred = starred == QStringLiteral("true")
            || starred == QStringLiteral("yes")
            || starred == QStringLiteral("1")
            || starred == QStringLiteral("starred");
        entry.frequency = fieldAt(row, frequencyColumn).toInt();
        entry.lastUsedMs = fieldAt(row, lastUsedColumn).toLongLong();
        entries.append(entry);
    }
    return normalizeVocabularyEntries(entries);
}

} // namespace speecher
