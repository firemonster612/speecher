#include "core/WordPreview.h"

#include <QRegularExpression>

namespace speecher {

QString WordPreview::lastWords(const QString &text, int count)
{
    if (count <= 0) {
        return {};
    }

    const QString simplified = text.simplified();
    if (simplified.isEmpty()) {
        return {};
    }

    const QStringList words = simplified.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.size() <= count) {
        return words.join(' ');
    }
    return words.mid(words.size() - count).join(' ');
}

} // namespace speecher
