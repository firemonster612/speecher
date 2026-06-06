#include "output/TextDelivery.h"

#include "core/AppSettings.h"
#include "core/OutputMethod.h"
#include "output/QtClipboardDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "output/WtypeDelivery.h"
#include "output/YdotoolDelivery.h"

#include <utility>

namespace speecher {

namespace {

class WtypeBackend final : public DeliveryBackend {
public:
    explicit WtypeBackend(QString command)
        : m_command(std::move(command))
    {
    }

    bool deliver(const QString &text, QString *error) override
    {
        return WtypeDelivery().deliver(m_command.isEmpty() ? QStringLiteral("wtype") : m_command, text, error);
    }

private:
    QString m_command;
};

class YdotoolBackend final : public DeliveryBackend {
public:
    bool deliver(const QString &text, QString *error) override
    {
        return YdotoolDelivery().type(text, error);
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
    if (method == QString::fromLatin1(OutputMethod::Wtype)) {
        return std::make_unique<WtypeBackend>(settings.typeCommand);
    }
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

DeliveryResult TextDelivery::deliver(const QString &command, const QString &text, bool allowClipboardFallback)
{
    OutputSettings settings;
    settings.method = allowClipboardFallback ? QString::fromLatin1(OutputMethod::Automatic) : QString::fromLatin1(OutputMethod::Wtype);
    settings.typeCommand = command;
    settings.fallbackClipboard = allowClipboardFallback;
    return deliver(settings, text);
}

QStringList TextDelivery::orderedMethods(const OutputSettings &settings)
{
    const QString method = OutputMethod::normalized(settings.method);
    if (method == QString::fromLatin1(OutputMethod::Automatic)) {
        QStringList methods{QString::fromLatin1(OutputMethod::Wtype)};
        if (settings.ydotoolEnabled) {
            methods << QString::fromLatin1(OutputMethod::Ydotool);
        }
        if (settings.fallbackClipboard) {
            methods << QString::fromLatin1(OutputMethod::WlCopy)
                    << QString::fromLatin1(OutputMethod::QtClipboard);
        }
        return methods;
    }

    if (method == QString::fromLatin1(OutputMethod::Ydotool) && !settings.ydotoolEnabled) {
        return {};
    }
    return {method};
}

} // namespace speecher
