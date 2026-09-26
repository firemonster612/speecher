#include "core/DictationRecord.h"

#include <QTextBoundaryFinder>

#include <algorithm>

namespace speecher {

int countWords(const QString &text)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
    int words = 0;
    qsizetype start = 0;
    for (qsizetype end = finder.toNextBoundary(); end != -1; end = finder.toNextBoundary()) {
        const QStringView segment = QStringView(text).sliced(start, end - start);
        if (std::any_of(segment.begin(), segment.end(), [](QChar c) { return c.isLetterOrNumber(); })) {
            ++words;
        }
        start = end;
    }
    return words;
}

} // namespace speecher
