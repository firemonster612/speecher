#pragma once

#include <QString>

namespace speecher {

struct RecordingStatus;
struct RecordingStream;

// "hh:mm:ss" since the recording started, for its lines and its duration.
QString recordingClock(qint64 elapsedMs);

// Shown once, on the first `record start` ever.
QString recordingConsentNotice();

// What `record status` prints: the file, how long it has run and each stream.
QString recordingStatusText(const RecordingStatus &status);
// What went wrong with a stream, a line each, for stderr: why it stopped or
// reconnects, and audio it lost. Empty when nothing did.
QString recordingStreamProblemText(const RecordingStream &stream);
// The lines the file is missing and why, for stderr. Empty when it has them
// all.
QString recordingWriteProblemText(const RecordingStatus &status);

QString notRecordingText();
QString alreadyRecordingText(const QString &path);
QString recordingNeedsSetupText();
QString recordingSpeechProviderRefusal(const QString &providerLabel);
QString recordingStreamEndedText();
QString recordingStopTimedOutText();
QString recordingFileError(const QString &path, const QString &reason);

} // namespace speecher
