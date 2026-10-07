#include "dictation/SelectionEditPresentation.h"

#include <QStringList>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace speecher {
namespace {

using Kind = EditRun::Kind;

// A review longer than this many words folds the unchanged ones away.
constexpr qsizetype kFoldAboveWords = 40;
// The unchanged words a folded review keeps either side of a change.
constexpr qsizetype kFoldContextWords = 6;
// Fewer words than this left out are not worth a gap.
constexpr qsizetype kFoldMinimumWords = 3;
// A review names the last few instructions, so follow-ups never push the
// card past the screen.
constexpr qsizetype kShownInstructions = 3;
// An edit that keeps less than this share of its words is a rewrite.
constexpr double kRewriteKeptShare = 0.5;
// The comparison table's limit in cells. Past it, past about two thousand
// changed words each way, the edit is shown whole as a rewrite.
constexpr qsizetype kMaxDiffCells = 4'000'000;

// The text cut into words, each with the spaces after it, and spaces before
// the first word with that word. Unicode's word boundaries, so punctuation is
// a word of its own and CJK text splits where it has no spaces.
QStringList words(const QString &text)
{
    QStringList words;
    QString leading;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
    qsizetype start = 0;
    for (qsizetype end = finder.toNextBoundary(); end > 0; end = finder.toNextBoundary()) {
        const QString piece = text.mid(start, end - start);
        start = end;
        if (!piece.trimmed().isEmpty()) {
            words.append(std::exchange(leading, QString()) + piece);
        } else if (words.isEmpty()) {
            leading += piece;
        } else {
            words.last() += piece;
        }
    }
    if (!leading.isEmpty()) {
        words.append(leading);
    }
    return words;
}

// Scripts written without spaces between words: a mark there needs no space
// beside it to stand apart.
bool writtenWithSpaces(QChar c)
{
    switch (c.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Thai:
    case QChar::Script_Lao:
    case QChar::Script_Khmer:
    case QChar::Script_Myanmar:
    case QChar::Script_Tibetan:
        return false;
    default:
        return c.isLetterOrNumber();
    }
}

// Two words are the same when only the spaces after them differ.
QStringList comparable(const QStringList &words)
{
    QStringList keys;
    keys.reserve(words.size());
    for (const QString &word : words) {
        keys.append(word.trimmed());
    }
    return keys;
}

// Words proper, not punctuation: a kept comma says nothing about how much
// of the text survived.
qsizetype countWords(const QStringList &words)
{
    return std::count_if(words.cbegin(), words.cend(), [](const QString &word) {
        return std::any_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetterOrNumber(); });
    });
}

// The longest run of words both share, Removed before Added where a stretch
// changed, after setting aside the start and end they share. Nothing when
// the stretch between is too long to compare.
std::optional<QList<EditRun>> diff(const QStringList &before, const QStringList &after)
{
    const QStringList beforeKeys = comparable(before);
    const QStringList afterKeys = comparable(after);
    qsizetype head = 0;
    while (head < before.size() && head < after.size() && beforeKeys[head] == afterKeys[head]) {
        ++head;
    }
    qsizetype tail = 0;
    while (tail < before.size() - head && tail < after.size() - head
           && beforeKeys[before.size() - 1 - tail] == afterKeys[after.size() - 1 - tail]) {
        ++tail;
    }
    const qsizetype n = before.size() - head - tail;
    const qsizetype m = after.size() - head - tail;
    if ((n + 1) * (m + 1) > kMaxDiffCells) {
        return std::nullopt;
    }
    // shared(i, j): the most words before[head + i..] and after[head + j..]
    // share in order, within the stretch between. Never more than the
    // shorter side, which the cell limit keeps within 16 bits.
    std::vector<quint16> table(size_t((n + 1) * (m + 1)), 0);
    const auto shared = [&table, m](qsizetype i, qsizetype j) -> quint16 & {
        return table[size_t(i * (m + 1) + j)];
    };
    for (qsizetype i = n - 1; i >= 0; --i) {
        for (qsizetype j = m - 1; j >= 0; --j) {
            shared(i, j) = beforeKeys[head + i] == afterKeys[head + j]
                ? quint16(shared(i + 1, j + 1) + 1)
                : std::max(shared(i + 1, j), shared(i, j + 1));
        }
    }

    QList<EditRun> runs;
    const auto add = [&runs](Kind kind, const QString &word) {
        if (!runs.isEmpty() && runs.last().kind == kind) {
            runs.last().text += word;
        } else {
            runs.append({kind, word});
        }
    };
    for (qsizetype k = 0; k < head; ++k) {
        add(Kind::Kept, after[k]);
    }
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && beforeKeys[head + i] == afterKeys[head + j]) {
            add(Kind::Kept, after[head + j]);
            ++i;
            ++j;
        } else if (i < n && (j == m || shared(i + 1, j) >= shared(i, j + 1))) {
            add(Kind::Removed, before[head + i++]);
        } else {
            add(Kind::Added, after[head + j++]);
        }
    }
    for (qsizetype k = after.size() - tail; k < after.size(); ++k) {
        add(Kind::Kept, after[k]);
    }
    return runs;
}

// A changed run's closing spaces leave its mark, and a removed word gets a
// space from a kept word it would otherwise touch, before or after it, so the
// two read as two words.
void separateMarks(QList<EditRun> &runs)
{
    for (qsizetype index = 0; index < runs.size(); ++index) {
        EditRun &run = runs[index];
        if (run.kind == Kind::Kept) {
            continue;
        }
        if (run.kind == Kind::Removed && index > 0 && !run.text.isEmpty() && writtenWithSpaces(run.text.front())) {
            QString &before = runs[index - 1].text;
            if (!before.isEmpty() && !before.back().isSpace()) {
                before += QLatin1Char(' ');
            }
        }
        qsizetype end = run.text.size();
        while (end > 0 && run.text.at(end - 1).isSpace()) {
            --end;
        }
        run.trailing = run.text.mid(end);
        run.text.truncate(end);
        const bool replaced = index + 1 < runs.size() && runs[index + 1].kind == Kind::Added;
        if (run.kind == Kind::Removed && replaced && run.trailing.isEmpty() && !run.text.isEmpty()
            && writtenWithSpaces(run.text.back())) {
            run.trailing = QStringLiteral(" ");
        }
    }
}

qsizetype changeCount(const QList<EditRun> &runs)
{
    qsizetype changes = 0;
    for (qsizetype index = 0; index < runs.size(); ++index) {
        if (runs[index].kind != Kind::Kept && (index == 0 || runs[index - 1].kind == Kind::Kept)) {
            ++changes;
        }
    }
    return changes;
}

qsizetype keptWords(const QList<EditRun> &runs)
{
    qsizetype kept = 0;
    for (const EditRun &run : runs) {
        if (run.kind == Kind::Kept) {
            kept += countWords(words(run.text));
        }
    }
    return kept;
}

QList<EditRun> fold(const QList<EditRun> &runs)
{
    qsizetype total = 0;
    for (const EditRun &run : runs) {
        total += countWords(words(run.text));
    }
    if (total <= kFoldAboveWords) {
        return {};
    }
    QList<EditRun> folded;
    for (qsizetype index = 0; index < runs.size(); ++index) {
        const EditRun &run = runs[index];
        const QStringList runWords = words(run.text);
        const qsizetype head = index == 0 ? 0 : kFoldContextWords;
        const qsizetype tail = index == runs.size() - 1 ? 0 : kFoldContextWords;
        if (run.kind != Kind::Kept || runWords.size() < head + tail + kFoldMinimumWords) {
            folded.append(run);
            continue;
        }
        if (head > 0) {
            folded.append({Kind::Kept, runWords.mid(0, head).join(QString())});
        }
        folded.append({Kind::Omitted, QStringLiteral("…"), QStringLiteral(" ")});
        if (tail > 0) {
            folded.append({Kind::Kept, runWords.mid(runWords.size() - tail).join(QString())});
        }
    }
    return folded == runs ? QList<EditRun>() : folded;
}

QString changesSummary(qsizetype changes)
{
    return changes == 1 ? QStringLiteral("1 change") : QStringLiteral("%1 changes").arg(changes);
}

} // namespace

SelectionEditReview selectionEditReview(const QString &original,
                                        const QString &revised,
                                        const QStringList &instructions,
                                        const ReviewKeys &keys)
{
    SelectionEditReview review;
    QStringList quoted;
    for (const QString &instruction : instructions) {
        if (const QString said = instruction.simplified(); !said.isEmpty()) {
            quoted.append(QStringLiteral("“%1”").arg(said));
        }
    }
    const bool earlier = quoted.size() > kShownInstructions;
    review.instruction = (earlier ? QStringLiteral("… then ") : QString())
        + quoted.mid(quoted.size() - std::min(quoted.size(), kShownInstructions)).join(QStringLiteral(" then "));
    review.keys = keys;
    review.followUpHint = keys.followUp.isEmpty()
        ? QString()
        : QStringLiteral("Press %1 to ask for more changes").arg(keys.followUp);
    const QStringList before = words(original);
    const QStringList after = words(revised);
    const std::optional<QList<EditRun>> runs = diff(before, after);
    const qsizetype changes = runs ? changeCount(*runs) : -1;
    if (changes == 0) {
        // The words are the same; only the spaces or line breaks between
        // them may differ, which the revised text shows as it is.
        review.runs = {{Kind::Kept, revised}};
        review.summary = original == revised ? QStringLiteral("No changes") : QStringLiteral("Spacing changes only");
        return review;
    }
    // Measured against the longer side, so cutting most of the words is a
    // rewrite as much as replacing them is.
    const qsizetype longerWords = std::max(countWords(before), countWords(after));
    if (!runs || keptWords(*runs) < kRewriteKeptShare * longerWords) {
        review.runs = {{Kind::Kept, revised}};
        review.summary = QStringLiteral("Rewritten");
        return review;
    }
    review.runs = *runs;
    separateMarks(review.runs);
    review.folded = fold(review.runs);
    review.summary = changesSummary(changes);
    return review;
}

QString keepOriginalCaption()
{
    return QStringLiteral("Keep original");
}

QString replaceSelectionCaption()
{
    return QStringLiteral("Replace");
}

QString showWholeEditCaption()
{
    return QStringLiteral("Show all");
}

QString showChangesOnlyCaption()
{
    return QStringLiteral("Show less");
}

QString escapeKeyName()
{
    return QStringLiteral("Esc");
}

QString enterKeyName()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Return");
#else
    return QStringLiteral("Enter");
#endif
}

ReviewKeys reviewKeysFor(bool escapeHeld, bool enterHeld, const QString &cancelShortcut,
                         const QString &globalShortcut)
{
    return {escapeHeld ? escapeKeyName() : cancelShortcut, enterHeld ? enterKeyName() : QString(), globalShortcut};
}

QString selectionChangedNote()
{
    return QStringLiteral("Your selection changed");
}

} // namespace speecher
