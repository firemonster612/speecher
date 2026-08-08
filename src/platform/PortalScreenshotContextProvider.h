#pragma once

#include "dictation/DictationPorts.h"

#include <QDBusObjectPath>

namespace speecher {

class PortalScreenshotContextProvider final : public ScreenshotContextProvider {
    Q_OBJECT

public:
    explicit PortalScreenshotContextProvider(QObject *parent = nullptr);

    void capture() override;
    void cancel() override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    void disconnectRequest();

    QDBusObjectPath m_requestPath;
    bool m_cancelled = false;
};

} // namespace speecher
