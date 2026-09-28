#include "dictation/PopupPresentation.h"

#include "core/WordPreview.h"

#include <QList>
#include <QTextBoundaryFinder>

namespace speecher {

QString trimPreviewToFit(const QString &preview, const std::function<bool(const QString &)> &fits)
{
    const QString whole = preview.simplified();
    if (whole.isEmpty() || fits(whole)) {
        return whole;
    }
    const QString ellipsis = QStringLiteral("… ");
    const int words = int(whole.count(QLatin1Char(' '))) + 1;
    // The most trailing words that fit; fewer words is always narrower.
    int low = 0;
    int high = words - 1;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (fits(ellipsis + WordPreview::lastWords(whole, middle))) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    if (low > 0) {
        return ellipsis + WordPreview::lastWords(whole, low);
    }
    // Not even the last word fits: keep as much of its end as does.
    const QString last = WordPreview::lastWords(whole, 1);
    const QString cut = QStringLiteral("…");
    QList<qsizetype> starts;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, last);
    for (qsizetype position = 0; position >= 0 && position < last.size();
         position = finder.toNextBoundary()) {
        starts.append(position);
    }
    for (const qsizetype start : std::as_const(starts)) {
        if (fits(cut + last.mid(start))) {
            return cut + last.mid(start);
        }
    }
    return cut;
}

} // namespace speecher
