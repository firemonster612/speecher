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
// Ends why a Recording runs without echo cancellation with the advice that
// headphones keep system audio out of the microphone.
QString withHeadphonesAdvice(const QString &why);
// Why a Recording's microphone runs without Windows' echo cancellation, for
// `record start` and `record status`: Windows would not open it for calls,
// with error; Windows cannot say whether it cancels echo; its driver has no
// echo cancellation; the driver has it turned off; or Windows would not
// point it at the default output, with error.
QString echoCancellationNoCallStreamText(quint32 error);
QString echoCancellationUnknownText();
QString echoCancellationNotOfferedText();
QString echoCancellationTurnedOffText();
QString echoCancellationNoReferenceText(quint32 error);

} // namespace speecher
