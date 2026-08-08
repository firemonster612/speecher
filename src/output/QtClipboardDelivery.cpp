#include "output/QtClipboardDelivery.h"

#include "output/DeliveryContent.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMimeData>
#include <QThread>

namespace speecher {

QtClipboardDelivery::QtClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool QtClipboardDelivery::copy(const QString &text, QString *error)
{
    return copy(DeliveryContent{text, std::nullopt}, error);
}

bool QtClipboardDelivery::copy(const DeliveryContent &content, QString *error)
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        if (error) {
            *error = QStringLiteral("Clipboard is unavailable");
        }
        return false;
    }

    constexpr int maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        auto *mimeData = new QMimeData;
        mimeData->setText(content.plainText);
        if (content.html) {
            mimeData->setHtml(*content.html);
        }
        clipboard->setMimeData(mimeData);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (attempt + 1 < maxAttempts) {
            QThread::msleep(40);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    return true;
}

} // namespace speecher
