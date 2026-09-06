#include "core/WordPreview.h"

namespace speecher {

QString WordPreview::lastWords(const QString &text, int count)
{
    if (count <= 0) return {};
    qsizetype start = text.size();
    while (start > 0 && count > 0) {
        while (start > 0 && text.at(start - 1).isSpace()) --start;
        while (start > 0 && !text.at(start - 1).isSpace()) --start;
        --count;
    }
    return text.mid(start).simplified();
}

} // namespace speecher
