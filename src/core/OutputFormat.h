#pragma once

#include <QString>

#include <optional>

namespace speecher {

enum class OutputFormat {
    PlainText,
    Html,
};

// "plain" or "html", in any case and with surrounding space; nothing for
// anything else.
std::optional<OutputFormat> outputFormatFromString(const QString &value);
QString outputFormatName(OutputFormat format);

} // namespace speecher
