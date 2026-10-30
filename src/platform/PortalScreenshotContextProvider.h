#pragma once

#include "dictation/DictationPorts.h"

#include <QPointer>
#include <QVariantMap>

namespace speecher {

class PortalRequest;

class PortalScreenshotContextProvider final : public ScreenshotContextProvider {
    Q_OBJECT

public:
    explicit PortalScreenshotContextProvider(QObject *parent = nullptr);

    void capture() override;
    void cancel() override;

private:
    void processResponse(uint response, const QVariantMap &results);

    QPointer<PortalRequest> m_request;
    quint64 m_generation = 0;
};

} // namespace speecher
