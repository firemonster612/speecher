#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/OutputMethod.h"
#include "output/ClipboardDelivery.h"
#include "output/QtClipboardDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "output/YdotoolDelivery.h"

namespace speecher {

namespace {

class YdotoolBackend final : public DeliveryBackend {
public:
    bool deliver(const QString &text, QString *error) override
    {
        QString copyError;
        if (!ClipboardDelivery().copy(text, &copyError)) {
            if (error) {
                *error = copyError.isEmpty()
                    ? QStringLiteral("Could not copy text before ydotool paste")
                    : QStringLiteral("Could not copy text before ydotool paste: %1").arg(copyError);
            }
            return false;
        }

        QString pasteError;
        if (!YdotoolDelivery().pasteFromClipboard(text, &pasteError)) {
            if (error) {
                *error = pasteError;
            }
            return false;
        }
        return true;
    }
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
    Q_UNUSED(settings)
    if (method == QString::fromLatin1(OutputMethod::Ydotool)) {
        return std::make_unique<YdotoolBackend>();
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
