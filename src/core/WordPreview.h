#pragma once

#include <QString>

namespace speecher {

class WordPreview {
public:
    static QString lastWords(const QString &text, int count);
};

} // namespace speecher
