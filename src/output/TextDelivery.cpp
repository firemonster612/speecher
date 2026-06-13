#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/OutputMethod.h"
#include "output/ClipboardDelivery.h"
#include "output/QtClipboardDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "output/YdotoolDelivery.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QMimeData>
#include <QThread>
#include <QUrl>

#include <memory>
#include <utility>

namespace speecher {

namespace {

constexpr int clipboardRestoreDelayMs = 250;

enum class ClipboardRestoreBackend {
    None,
    WlClipboard,
    QtClipboard,
};

struct RestorableClipboard {
    ClipboardRestoreBackend backend = ClipboardRestoreBackend::None;
    WlClipboardSnapshot wlSnapshot;
    std::unique_ptr<QMimeData> qtSnapshot;
};

QClipboard *applicationClipboard(QString *error)
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard && error) {
        *error = QStringLiteral("Qt clipboard is not available");
    }
    return clipboard;
}

std::unique_ptr<QMimeData> cloneMimeData(const QMimeData *source)
{
    auto clone = std::make_unique<QMimeData>();
    if (!source) {
        return clone;
    }

    for (const QString &format : source->formats()) {
        clone->setData(format, source->data(format));
    }
    if (source->hasText()) {
        clone->setText(source->text());
    }
    if (source->hasHtml()) {
        clone->setHtml(source->html());
    }
    if (source->hasUrls()) {
        clone->setUrls(source->urls());
    }
    if (source->hasImage()) {
        clone->setImageData(source->imageData());
    }
    if (source->hasColor()) {
        clone->setColorData(source->colorData());
    }
    return clone;
}

std::unique_ptr<QMimeData> captureClipboardMimeData(QString *error)
{
    QClipboard *clipboard = applicationClipboard(error);
    if (!clipboard) {
        return nullptr;
    }
    return cloneMimeData(clipboard->mimeData(QClipboard::Clipboard));
}

bool restoreClipboardMimeData(std::unique_ptr<QMimeData> mimeData, QString *error)
{
    QClipboard *clipboard = applicationClipboard(error);
    if (!clipboard) {
        return false;
    }
    if (!mimeData) {
        clipboard->clear(QClipboard::Clipboard);
        return true;
    }
    clipboard->setMimeData(mimeData.release(), QClipboard::Clipboard);
    return true;
}

RestorableClipboard captureRestorableClipboard()
{
    RestorableClipboard clipboard;
    if (WlClipboardDelivery::canSnapshot()) {
        QString wlError;
        if (WlClipboardDelivery::capture(&clipboard.wlSnapshot, &wlError)) {
            clipboard.backend = ClipboardRestoreBackend::WlClipboard;
            return clipboard;
        }
        qWarning().noquote() << "wl-clipboard restore capture failed:" << wlError;
    }

    QString qtError;
    clipboard.qtSnapshot = captureClipboardMimeData(&qtError);
    if (clipboard.qtSnapshot) {
        clipboard.backend = ClipboardRestoreBackend::QtClipboard;
        return clipboard;
    }

    qWarning().noquote() << "clipboard restore capture failed:" << qtError;
    return {};
}

void restoreClipboard(RestorableClipboard *clipboard)
{
    if (!clipboard) {
        return;
    }

    switch (clipboard->backend) {
    case ClipboardRestoreBackend::WlClipboard: {
        QString restoreError;
        if (!WlClipboardDelivery::restore(clipboard->wlSnapshot, &restoreError)) {
            qWarning().noquote() << "wl-clipboard restore failed:" << restoreError;
        }
        clipboard->backend = ClipboardRestoreBackend::None;
        break;
    }
    case ClipboardRestoreBackend::QtClipboard: {
        QString restoreError;
        if (!restoreClipboardMimeData(std::move(clipboard->qtSnapshot), &restoreError)) {
            qWarning().noquote() << "clipboard restore failed:" << restoreError;
        }
        clipboard->backend = ClipboardRestoreBackend::None;
        break;
    }
    case ClipboardRestoreBackend::None:
        break;
    }
}

bool hasRestorableClipboard(const RestorableClipboard &clipboard)
{
    return clipboard.backend != ClipboardRestoreBackend::None;
}

class YdotoolBackend final : public DeliveryBackend {
public:
    explicit YdotoolBackend(bool restoreClipboardAfterTyping)
        : m_restoreClipboardAfterTyping(restoreClipboardAfterTyping)
    {
    }

    bool deliver(const QString &text, QString *error) override
    {
        RestorableClipboard previousClipboard;
        if (m_restoreClipboardAfterTyping) {
            previousClipboard = captureRestorableClipboard();
        }

        QString copyError;
        if (!ClipboardDelivery().copy(text, &copyError)) {
            if (error) {
                *error = copyError.isEmpty()
                    ? QStringLiteral("Could not copy text before ydotool paste")
                    : QStringLiteral("Could not copy text before ydotool paste: %1").arg(copyError);
            }
            return false;
        }

        const auto restorePreviousClipboard = [&previousClipboard] {
            if (!hasRestorableClipboard(previousClipboard)) {
                return;
            }
            QThread::msleep(clipboardRestoreDelayMs);
            restoreClipboard(&previousClipboard);
        };

        QString pasteError;
        if (!YdotoolDelivery().pasteFromClipboard(text, &pasteError)) {
            restorePreviousClipboard();
            if (error) {
                *error = pasteError;
            }
            return false;
        }
        restorePreviousClipboard();
        return true;
    }

private:
    bool m_restoreClipboardAfterTyping = false;
};

class WlCopyBackend final : public DeliveryBackend {
public:
    bool deliver(const QString &text, QString *error) override
    {
        return WlClipboardDelivery().copy(text, error);
    }
};

class QtClipboardBackend final : public DeliveryBackend {
public:
    bool deliver(const QString &text, QString *error) override
    {
        return QtClipboardDelivery().copy(text, error);
    }
};

std::unique_ptr<DeliveryBackend> defaultBackendFactory(const QString &method, const OutputSettings &settings)
{
    if (method == QString::fromLatin1(OutputMethod::Ydotool)) {
        return std::make_unique<YdotoolBackend>(settings.restoreClipboardAfterTyping);
    }
    if (method == QString::fromLatin1(OutputMethod::WlCopy)) {
        return std::make_unique<WlCopyBackend>();
    }
    if (method == QString::fromLatin1(OutputMethod::QtClipboard)) {
        return std::make_unique<QtClipboardBackend>();
    }
    return nullptr;
}

} // namespace

TextDelivery::TextDelivery(QObject *parent)
    : TextDeliveryAdapter(parent)
    , m_backendFactory(defaultBackendFactory)
{
}

TextDelivery::TextDelivery(BackendFactory backendFactory, QObject *parent)
    : TextDeliveryAdapter(parent)
    , m_backendFactory(std::move(backendFactory))
{
}

DeliveryResult TextDelivery::deliver(const OutputSettings &settings, const QString &text)
{
    QString firstError;
    for (const QString &method : orderedMethods(settings)) {
        std::unique_ptr<DeliveryBackend> backend = m_backendFactory ? m_backendFactory(method, settings) : nullptr;
        if (!backend) {
            continue;
        }

        QString error;
        if (backend->deliver(text, &error)) {
            return {true,
                    method == QString::fromLatin1(OutputMethod::WlCopy) || method == QString::fromLatin1(OutputMethod::QtClipboard),
                    method == QString::fromLatin1(OutputMethod::WlCopy) || method == QString::fromLatin1(OutputMethod::QtClipboard)
                        ? QStringLiteral("Copied")
                        : QStringLiteral("Delivered")};
        }
        if (firstError.isEmpty()) {
            firstError = error;
        }
    }
    return {false, false, firstError.isEmpty() ? QStringLiteral("No output method is available") : firstError};
}

QStringList TextDelivery::orderedMethods(const OutputSettings &settings)
{
    const QString method = OutputMethod::normalized(settings.method);
    if (method == QString::fromLatin1(OutputMethod::Automatic)) {
        QStringList methods;
        if (settings.ydotoolEnabled) {
            methods << QString::fromLatin1(OutputMethod::Ydotool);
        }
        methods << QString::fromLatin1(OutputMethod::WlCopy)
                << QString::fromLatin1(OutputMethod::QtClipboard);
        return methods;
    }

    if (method == QString::fromLatin1(OutputMethod::Ydotool) && !settings.ydotoolEnabled) {
        return {};
    }
    return {method};
}

} // namespace speecher
