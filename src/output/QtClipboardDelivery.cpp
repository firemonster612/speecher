#include "output/QtClipboardDelivery.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

namespace speecher {

QtClipboardDelivery::QtClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool QtClipboardDelivery::copy(const QString &text, QString *error)
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
        clipboard->setText(text);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (attempt + 1 < maxAttempts) {
            QThread::msleep(40);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    return true;
}

} // namespace speecher
