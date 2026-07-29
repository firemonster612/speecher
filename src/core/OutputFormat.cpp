#include "core/OutputFormat.h"

namespace speecher {

OutputFormat outputFormatFromString(const QString &value)
{
    return value.trimmed().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
        ? OutputFormat::Html
        : OutputFormat::PlainText;
}

QString outputFormatName(OutputFormat format)
{
    return format == OutputFormat::Html ? QStringLiteral("html") : QStringLiteral("plain");
}

} // namespace speecher
