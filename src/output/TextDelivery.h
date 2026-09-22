#pragma once

#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"

#include <functional>
#include <memory>
#include <QStringList>

namespace speecher {

class DeliveryBackend {
public:
    virtual ~DeliveryBackend() = default;
    // clearToInject is the final focus gate: a keyboard backend calls it after
    // all of its blocking preparation (clipboard helper startup, owner
    // replacement, modifier release) and immediately before sending the paste
    // keystroke, aborting when it returns false. Clipboard-only backends may
    // ignore it; a null function means nothing gates the injection.
    virtual bool deliver(const DeliveryContent &content,
                         const std::function<bool()> &clearToInject,
                         bool *htmlAvailable,
                         QString *error = nullptr) = 0;
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
