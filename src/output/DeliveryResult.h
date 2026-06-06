#pragma once

#include <QString>

namespace speecher {

struct DeliveryResult {
    bool ok = false;
    bool copied = false;
    QString message;
};

} // namespace speecher
