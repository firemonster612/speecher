#include "dictation/PopupPresentation.h"

#include <QList>
#include <QTextBoundaryFinder>

namespace speecher {

namespace {

// Where the text may be cut, in order: each position a word starts at, which
// Unicode puts between CJK words as well as after spaces.
QList<qsizetype> boundaries(QTextBoundaryFinder::BoundaryType type, const QString &text,
                            bool wordStartsOnly)
{
    QList<qsizetype> starts;
    QTextBoundaryFinder finder(type, text);
    for (qsizetype position = 0; position >= 0 && position < text.size();
         position = finder.toNextBoundary()) {
        if (!wordStartsOnly || finder.boundaryReasons().testFlag(QTextBoundaryFinder::StartOfItem)
            || position == 0) {
            starts.append(position);
        }
    }
    return starts;
}

// The longest tail from these cut points that fits, or empty. Never the whole
// text (the first cut point), which the caller already found too wide.
QString longestFittingTail(const QString &text, const QList<qsizetype> &starts,
                           const QString &prefix,
                           const std::function<bool(const QString &)> &fits)
{
    // Later starts are shorter tails, so the first that fits is the longest.
    qsizetype low = 1;
    qsizetype high = starts.size();
    while (low < high) {
        const qsizetype middle = (low + high) / 2;
        if (fits(prefix + text.mid(starts[middle]).trimmed())) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    if (low < starts.size()) {
        const QString tail = prefix + text.mid(starts[low]).trimmed();
        if (fits(tail)) {
            return tail;
        }
    }
    return {};
}

} // namespace

QString trimPreviewToFit(const QString &preview, const std::function<bool(const QString &)> &fits)
{
    const QString whole = preview.simplified();
    if (whole.isEmpty() || fits(whole)) {
        return whole;
    }
    const QString atWord = longestFittingTail(
        whole, boundaries(QTextBoundaryFinder::Word, whole, true), QStringLiteral("… "), fits);
    if (!atWord.isEmpty()) {
        return atWord;
    }
    // Not even the last word fits: keep as much of its end as does.
    const QString atGrapheme = longestFittingTail(
        whole, boundaries(QTextBoundaryFinder::Grapheme, whole, false), QStringLiteral("…"), fits);
    return atGrapheme.isEmpty() ? QStringLiteral("…") : atGrapheme;
}

} // namespace speecher
