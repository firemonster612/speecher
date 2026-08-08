#pragma once

#include <QString>

namespace speecher {

enum class DeliveryReceipt {
    None,
    Copied,
    InputSent,
    AcceptedByTarget,
    VerifiedInTarget,
};

struct DeliveryResult {
    bool ok = false;
    DeliveryReceipt receipt = DeliveryReceipt::None;
    bool formatDowngraded = false;
    QString message;
};

} // namespace speecher
