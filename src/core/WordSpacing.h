#pragma once

#include <QChar>

namespace speecher {

// Whether a script is written without spaces between its words, so text in
// it needs no space to stand apart from what is beside it.
inline bool writtenWithoutSpaces(QChar::Script script)
{
    switch (script) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Thai:
    case QChar::Script_Lao:
    case QChar::Script_Khmer:
    case QChar::Script_Myanmar:
    case QChar::Script_Tibetan:
        return true;
    default:
        return false;
    }
}

} // namespace speecher
