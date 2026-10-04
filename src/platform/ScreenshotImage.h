#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

class QObject;

namespace speecher {

// Re-encodes a captured screenshot as a PNG no larger than the model context
// can afford. Returns empty when the source could not be decoded. Every
// screenshot provider hands its capture through here so the transcription
// providers see one format and one size ceiling.
QByteArray normalizedScreenshot(const QByteArray &source);

struct ScreenshotResult {
    QByteArray png;
    QString error;
};

// Reads, deletes and normalizes the capture file a screenshot tool wrote, then
// hands the result to done on receiver's thread. The work runs on a worker
// thread: decoding and re-encoding a whole-desktop capture takes hundreds of
// milliseconds, and the microphone is read on the main thread, so doing it
// there cuts out whatever the person says meanwhile.
void normalizeScreenshotFile(const QString &path,
                             QObject *receiver,
                             std::function<void(const ScreenshotResult &)> done);

} // namespace speecher
