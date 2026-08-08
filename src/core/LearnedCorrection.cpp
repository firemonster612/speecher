#include "core/LearnedCorrection.h"

namespace speecher {

std::optional<QString> correctionBetweenAnchors(const QString &window,
                                                const QString &prefix,
                                                const QString &suffix,
                                                const QString &original)
{
    if (prefix.size() < 8 || suffix.size() < 8) {
        return std::nullopt;
    }
    const int prefixAt = window.indexOf(prefix);
    if (prefixAt < 0 || window.indexOf(prefix, prefixAt + 1) >= 0) {
        return std::nullopt;
    }
    const int correctedStart = prefixAt + prefix.size();
    const int suffixAt = window.indexOf(suffix, correctedStart);
    if (suffixAt < correctedStart || window.indexOf(suffix, suffixAt + 1) >= 0) {
        return std::nullopt;
    }
    const QString corrected = window.mid(correctedStart, suffixAt - correctedStart).trimmed();
    if (corrected.isEmpty() || corrected == original.trimmed() || corrected.size() > 500) {
        return std::nullopt;
    }
    return corrected;
}

} // namespace speecher
