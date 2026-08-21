#pragma once

#include "dictation/DictationPorts.h"

#include <QDBusContext>
#include <QDBusObjectPath>

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

private:
    void disconnectRequest();

    QDBusObjectPath m_requestPath;
    quint64 m_generation = 0;
};

} // namespace speecher
