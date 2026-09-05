#pragma once

#include "core/PasteRules.h"

#include <QString>

namespace speecher {

class WinPasteDelivery {
public:
    bool paste(PasteMethod method, QString *error = nullptr);
};

} // namespace speecher
