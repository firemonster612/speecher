#pragma once

#include "dictation/DictationPorts.h"
#include "platform/PortalResponseTracker.h"

#include <QDBusContext>
#include <QDBusObjectPath>

class QTimer;

namespace speecher {

class PortalScreenshotContextProvider final : public ScreenshotContextProvider,
                                              protected QDBusContext {
    Q_OBJECT

public:
    explicit PortalScreenshotContextProvider(QObject *parent = nullptr);

    void capture() override;
    void cancel() override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);
    void handleTimeout();

private:
    void processResponse(const PortalResponse &response);
    void disconnectRequest();

    QDBusObjectPath m_requestPath;
    PortalResponseTracker m_responseTracker;
    QTimer *m_requestTimer = nullptr;
    quint64 m_generation = 0;
};

} // namespace speecher
