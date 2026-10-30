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

QString withHeadphonesAdvice(const QString &why)
{
    return why + QStringLiteral(" On speakers, the other side may also be written as you; headphones avoid it.");
}

QString echoCancellationNoCallStreamText(quint32 error)
{
    return withHeadphonesAdvice(
        QStringLiteral("Echo cancellation is off: Windows would not open the microphone for calls (error 0x%1).")
            .arg(error, 8, 16, QLatin1Char('0')));
}

QString echoCancellationUnknownText()
{
    return withHeadphonesAdvice(
        QStringLiteral("Echo cancellation may be off: Windows cannot say whether the microphone cancels echo."));
}

QString echoCancellationNotOfferedText()
{
    return withHeadphonesAdvice(QStringLiteral("Echo cancellation is off: the microphone's driver does not offer it."));
}

QString echoCancellationTurnedOffText()
{
    return withHeadphonesAdvice(QStringLiteral("Echo cancellation is off: the microphone's driver has it turned off."));
}

QString echoCancellationNoReferenceText(quint32 error)
{
    return withHeadphonesAdvice(
        QStringLiteral("Echo cancellation may miss system audio: Windows would not point it at the sound output "
                       "(error 0x%1).")
            .arg(error, 8, 16, QLatin1Char('0')));
}

} // namespace speecher
