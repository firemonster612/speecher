#include "core/Vocabulary.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <QUuid>

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

QStringList offeredVocabularyProfiles(const QStringList &ids,
                                      const QList<WritingProfileSettings> &profiles)
{
    QStringList offered;
    for (const QString &id : ids) {
        if (!offeredWritingProfile(id, profiles, QString()).isEmpty()) {
            offered.append(id);
        }
    }
    return offered;
}

QStringList parseVocabularyFile(const QByteArray &text)
{
    QStringList terms;
    for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        const QString term = line.simplified();
        if (!term.isEmpty() && !term.startsWith(QLatin1Char('#'))) {
            terms.append(term);
        }
    }
    return terms;
}

std::optional<QStringList> readVocabularyFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray text = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return std::nullopt;
    }
    return parseVocabularyFile(text);
}

QList<VocabularyEntry> normalizeVocabularyEntries(const QList<VocabularyEntry> &entries)
{
    QList<VocabularyEntry> spellings;
    QList<VocabularyEntry> replacements;
    for (VocabularyEntry entry : entries) {
        entry.source = entry.source.simplified();
        if (entry.source.isEmpty()) {
            entry.source = QStringLiteral("manual");
        }
        entry.frequency = qMax(0, entry.frequency);
        entry.lastUsedMs = qMax<qint64>(0, entry.lastUsedMs);
        entry.context = entry.context.trimmed();
        entry.profiles.removeAll(QString());
        entry.profiles.removeDuplicates();
        entry.apps.removeAll(QString());
        entry.apps.removeDuplicates();
        if (entry.replacement.trimmed().isEmpty()) {
            entry.replacement.clear();
        }
        // A phrase keeps its spacing for the matcher to fold; a term is
        // compared and sent simplified.
        entry.term = entry.isReplacement() ? entry.term.trimmed() : entry.term.simplified();
        if (entry.term.isEmpty()) {
            continue;
        }
        if (entry.isReplacement()) {
            replacements.append(entry);
            continue;
        }
        auto duplicate = std::find_if(spellings.begin(), spellings.end(), [&entry](const VocabularyEntry &existing) {
            return existing.term.compare(entry.term, Qt::CaseInsensitive) == 0;
        });
        if (duplicate == spellings.end()) {
            spellings.append(entry);
        } else {
            duplicate->starred = duplicate->starred || entry.starred;
            duplicate->frequency = qMax(duplicate->frequency, entry.frequency);
            duplicate->lastUsedMs = qMax(duplicate->lastUsedMs, entry.lastUsedMs);
            // The copy already listed keeps its Key term, context and
            // limits, so an imported row naming the term cannot send it to
            // the speech service or lift its limit. A context
            // it lacks comes from the later copy.
            if (duplicate->context.isEmpty()) {
                duplicate->context = entry.context;
            }
        }
    }
    // Priority only counts for a key term; one whose Key term is off keeps
    // its Priority stored but takes its place among the rest.
    std::sort(spellings.begin(), spellings.end(), [](const VocabularyEntry &left, const VocabularyEntry &right) {
        const bool leftFirst = left.starred && left.keyTerm;
        const bool rightFirst = right.starred && right.keyTerm;
        if (leftFirst != rightFirst) {
            return leftFirst;
        }
        if (left.frequency != right.frequency) {
            return left.frequency > right.frequency;
        }
        if (left.lastUsedMs != right.lastUsedMs) {
            return left.lastUsedMs > right.lastUsedMs;
        }
        return left.term.toCaseFolded() < right.term.toCaseFolded();
    });

    // Every entry is kept. The service cap applies to the subset sent with a
    // request (speechVocabulary), not to what we store, so a
    // term that does not fit today is still here when the list gets shorter.
    return spellings + replacements;
}

QList<VocabularyEntry> withVocabularyIds(QList<VocabularyEntry> entries)
{
    for (VocabularyEntry &entry : entries) {
        if (entry.id.isEmpty()) {
            entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }
    return entries;
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
    const int contextColumn = hasHeader ? column(header, QStringLiteral("context")) : 5;

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
        entry.context = fieldAt(row, contextColumn);
        entries.append(entry);
    }
    return normalizeVocabularyEntries(entries);
}

} // namespace speecher
