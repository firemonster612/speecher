#include "output/QtClipboardDelivery.h"

#include "dictation/DictationPorts.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>

namespace speecher {

QtClipboardDelivery::QtClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool QtClipboardDelivery::copy(const DeliveryContent &content, QString *error)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        if (error) {
            *error = QStringLiteral("Clipboard is unavailable");
        }
        return false;
    }

    auto *mimeData = new QMimeData;
    mimeData->setText(content.plainText);
    if (content.html) {
        mimeData->setHtml(*content.html);
    }
    clipboard->setMimeData(mimeData);
    return true;
}

} // namespace speecher
