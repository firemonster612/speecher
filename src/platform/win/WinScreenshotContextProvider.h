#pragma once

#include "dictation/DictationPorts.h"

#include <QPointer>
#include <QThread>

namespace speecher {

class WinScreenshotContextProvider : public ScreenshotContextProvider {
    Q_OBJECT

public:
    explicit WinScreenshotContextProvider(QObject *parent = nullptr);

    void capture() override;
    void cancel() override;

private:
    // The newest capture's worker, until it finishes and is deleted.
    QPointer<QThread> m_worker;
    // Whether that worker's result is still to be reported.
    bool m_wanted = false;
};

} // namespace speecher
