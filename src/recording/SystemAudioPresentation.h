#pragma once

#include <QString>

namespace speecher {

// Why system audio could not be captured or stopped, with reason, the
// platform's own words for what went wrong. Every platform's system audio
// input says it the same way.
QString systemAudioCaptureFailedText(const QString &reason);
QString systemAudioCaptureStoppedText(const QString &reason);
// Why system audio has nowhere to come from: the default sound output could
// not be followed, with reason, or there is none.
QString defaultSoundOutputUnfollowedText(const QString &reason);
QString noSoundOutputText();

} // namespace speecher
