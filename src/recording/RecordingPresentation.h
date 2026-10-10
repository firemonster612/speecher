#pragma once

#include <QString>

namespace speecher {

struct RecordingStatus;
struct RecordingStream;

// "hh:mm:ss" since the recording started, for its lines and its duration.
QString recordingClock(qint64 elapsedMs);

// Who a recording's lines name: the microphone's speaker, and system audio's,
// everyone else on the call.
QString recordingMicrophoneSpeaker();
QString recordingSystemAudioSpeaker();
// The microphone's line when a dictation takes it during a recording.
QString recordingDictatingText();

// Shown once, on the first `record start` ever.
QString recordingConsentNotice();

// What `record status` prints: the file, how long it has run, each stream
// and the echo cancellation warning.
QString recordingStatusText(const RecordingStatus &status);
// What went wrong with a stream, a line each, for stderr: why it stopped or
// reconnects, and audio it lost. Empty when nothing did.
QString recordingStreamProblemText(const RecordingStream &stream);
// The lines the file is missing and why, for stderr. Empty when it has them
// all.
QString recordingWriteProblemText(const RecordingStatus &status);
// A line of `status --watch`: the dictation state, or while recording its
// duration, each stream that is not recording and whether echo cancellation
// is off, after the dictation state unless that is idle.
QString statusWatchText(const QString &dictationState, const RecordingStatus &recording);

QString notRecordingText();
QString alreadyRecordingText(const QString &path);
QString recordingNeedsSetupText();
QString recordingSpeechProviderRefusal(const QString &providerLabel);
QString recordingStreamEndedText();
// A stream's problem once an utterance was not transcribed, for reason.
QString recordingUtteranceFailedText(const QString &reason);
QString recordingStopTimedOutText();
QString recordingFileError(const QString &path, const QString &reason);
// Why a Recording runs without echo cancellation, for `record start` and
// `record status`: a build without webrtc-audio-processing, one whose
// audio processing failed to start with error, or system audio that stopped.
QString echoCancellationNotBuiltText();
QString echoCancellationFailedText(int error);
QString echoCancellationWithoutSystemAudioText();
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
