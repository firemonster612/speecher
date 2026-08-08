#pragma once

#include "core/OutputFormat.h"

#include <QString>

#include <optional>

namespace speecher {

struct DeliveryContent {
    QString plainText;
    std::optional<QString> html;
};

DeliveryContent makeDeliveryContent(const QString &text, OutputFormat format);

} // namespace speecher
