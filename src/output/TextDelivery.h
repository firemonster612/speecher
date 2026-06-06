#pragma once

#include "dictation/DictationInterfaces.h"

namespace speecher {

class TextDelivery : public TextDeliveryAdapter {
    Q_OBJECT

public:
    explicit TextDelivery(QObject *parent = nullptr);
    DeliveryResult deliver(const OutputSettings &settings, const QString &text) override;
    DeliveryResult deliver(const QString &command, const QString &text, bool allowClipboardFallback);
};

} // namespace speecher
