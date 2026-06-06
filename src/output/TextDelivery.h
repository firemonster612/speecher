#pragma once

#include "dictation/DictationInterfaces.h"

#include <functional>
#include <memory>
#include <QStringList>

namespace speecher {

class DeliveryBackend {
public:
    virtual ~DeliveryBackend() = default;
    virtual bool deliver(const QString &text, QString *error = nullptr) = 0;
};

class TextDelivery : public TextDeliveryAdapter {
    Q_OBJECT

public:
    using BackendFactory = std::function<std::unique_ptr<DeliveryBackend>(const QString &method, const OutputSettings &settings)>;

    explicit TextDelivery(QObject *parent = nullptr);
    explicit TextDelivery(BackendFactory backendFactory, QObject *parent = nullptr);
    DeliveryResult deliver(const OutputSettings &settings, const QString &text) override;
    DeliveryResult deliver(const QString &command, const QString &text, bool allowClipboardFallback);
    static QStringList orderedMethods(const OutputSettings &settings);

private:
    BackendFactory m_backendFactory;
};

} // namespace speecher
