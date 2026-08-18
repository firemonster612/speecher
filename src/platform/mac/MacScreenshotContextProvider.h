#pragma once

#include "dictation/DictationPorts.h"

#include <QString>

class QProcess;

namespace speecher {

// Captures the screen with /usr/sbin/screencapture. The first capture triggers
// the Screen Recording prompt; until it is granted macOS either fails the tool
// or hands back a wallpaper-only image, so failures point at that pane.
class MacScreenshotContextProvider : public ScreenshotContextProvider {
    Q_OBJECT

public:
    explicit MacScreenshotContextProvider(QObject *parent = nullptr);

    void capture() override;
    void cancel() override;

private:
    void finish(int exitCode);
    void discardCaptureFile();

    QProcess *m_capture = nullptr;
    QString m_capturePath;
};

} // namespace speecher
