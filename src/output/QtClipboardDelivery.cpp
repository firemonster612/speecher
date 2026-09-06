#include "output/QtClipboardDelivery.h"

#include "dictation/DictationPorts.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUuid>

namespace speecher {
namespace {
constexpr auto copyIdMimeType = "application/x-speecher-copy-id";
}


QtClipboardDelivery::QtClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool QtClipboardDelivery::ownsClipboardContent() const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mime = clipboard ? clipboard->mimeData() : nullptr;
    // macOS does not notify a background app when another app copies. Reading
    // the pasteboard marker checks ownership even while Speecher is inactive.
    return mime && !m_copyId.isEmpty() && mime->data(copyIdMimeType) == m_copyId;
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
    m_copyId = QUuid::createUuid().toByteArray();
    mimeData->setData(copyIdMimeType, m_copyId);
    clipboard->setMimeData(mimeData);
    return true;
}

} // namespace speecher
