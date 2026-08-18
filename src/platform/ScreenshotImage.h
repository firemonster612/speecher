#pragma once

#include <QByteArray>

namespace speecher {

// Re-encodes a captured screenshot as a PNG no larger than the model context
// can afford. Returns empty when the source could not be decoded. Every
// screenshot provider hands its capture through here so the transcription
// providers see one format and one size ceiling.
QByteArray normalizedScreenshot(const QByteArray &source);

} // namespace speecher
