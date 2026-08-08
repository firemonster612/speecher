#pragma once

#include <QString>

namespace speecher {

enum class OutputFormat {
    PlainText,
    Html,
};

OutputFormat outputFormatFromString(const QString &value);
QString outputFormatName(OutputFormat format);

} // namespace speecher
