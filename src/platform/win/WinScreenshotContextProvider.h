#pragma once

#include "dictation/DictationPorts.h"

class QThread;

namespace speecher {

class WinScreenshotContextProvider : public ScreenshotContextProvider {
    Q_OBJECT

public:
    explicit WinScreenshotContextProvider(QObject *parent = nullptr);
    ~WinScreenshotContextProvider() override;

    void capture() override;
    void cancel() override;

private:
    QThread *m_capture = nullptr;
};

} // namespace speecher
