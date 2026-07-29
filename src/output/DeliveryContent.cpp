#include "output/DeliveryContent.h"

#include <QStringList>

namespace speecher {

namespace {

QString plainTextAsHtml(const QString &text)
{
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList paragraphs;
    for (const QString &paragraph : normalized.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        QString escaped = paragraph.trimmed().toHtmlEscaped();
        escaped.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        if (!escaped.isEmpty()) {
            paragraphs.append(QStringLiteral("<p>%1</p>").arg(escaped));
        }
    }
    return paragraphs.join(QLatin1Char('\n'));
}

} // namespace

DeliveryContent makeDeliveryContent(const QString &text, OutputFormat format)
{
    DeliveryContent content;
    content.plainText = text;
    if (format == OutputFormat::Html) {
        content.html = plainTextAsHtml(text);
    }
    return content;
}

} // namespace speecher
