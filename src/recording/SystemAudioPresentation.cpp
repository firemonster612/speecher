#include "recording/SystemAudioPresentation.h"

namespace speecher {

QString systemAudioCaptureFailedText(const QString &reason)
{
    return QStringLiteral("Could not capture system audio: %1").arg(reason);
}

QString systemAudioCaptureStoppedText(const QString &reason)
{
    return QStringLiteral("System audio capture stopped: %1").arg(reason);
}

QString defaultSoundOutputUnfollowedText(const QString &reason)
{
    return QStringLiteral("Could not follow the default sound output: %1").arg(reason);
}

QString noSoundOutputText()
{
    return QStringLiteral("There is no sound output to capture system audio from.");
}

} // namespace speecher
