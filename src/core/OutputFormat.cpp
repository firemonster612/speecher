#include "core/OutputFormat.h"

namespace speecher {

std::optional<OutputFormat> outputFormatFromString(const QString &value)
{
    const QString name = value.trimmed();
    for (const OutputFormat format : {OutputFormat::PlainText, OutputFormat::Html}) {
        if (name.compare(outputFormatName(format), Qt::CaseInsensitive) == 0) {
            return format;
        }
    }
    return std::nullopt;
}

QString outputFormatName(OutputFormat format)
{
    return format == OutputFormat::Html ? QStringLiteral("html") : QStringLiteral("plain");
}

} // namespace speecher
