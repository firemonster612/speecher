#pragma once

#include "dictation/DictationInterfaces.h"
#include "output/ClipboardDelivery.h"
#include "output/DeliveryContent.h"

#include <functional>
#include <memory>
#include <QStringList>

namespace speecher {

class DeliveryBackend {
public:
    virtual ~DeliveryBackend() = default;
    virtual bool deliver(const DeliveryContent &content, bool *htmlAvailable, QString *error = nullptr) = 0;
};

class TextDelivery : public TextDeliveryAdapter {
    Q_OBJECT

public:
    using BackendFactory = std::function<std::unique_ptr<DeliveryBackend>(
        const QString &method,
        const OutputSettings &settings,
        PasteMethod pasteMethod)>;

    explicit TextDelivery(QObject *parent = nullptr);
    explicit TextDelivery(TargetProvider *targetProvider, QObject *parent = nullptr);
    explicit TextDelivery(BackendFactory backendFactory, QObject *parent = nullptr);
    TextDelivery(BackendFactory backendFactory, TargetProvider *targetProvider, QObject *parent = nullptr);
    DeliveryResult deliver(const OutputSettings &settings,
                           const DeliveryContent &content,
                           const Target &target) override;
    static QStringList orderedMethods(const OutputSettings &settings);
    static QStringList orderedMethods(const OutputSettings &settings, PasteMethod pasteMethod);

private:
    void useDefaultBackendFactory();

    ClipboardDelivery m_clipboardDelivery;
    BackendFactory m_backendFactory;
    TargetProvider *m_targetProvider = nullptr;
};

} // namespace speecher
