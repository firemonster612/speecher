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
// A stream's problem, for stderr; empty when it has none.
QString recordingStreamProblemText(const RecordingStream &stream);

QString notRecordingText();
QString alreadyRecordingText(const QString &path);
QString recordingNeedsSetupText();
QString batchSpeechProviderRefusal(const QString &providerLabel);
QString recordingStreamEndedText();
QString recordingFileError(const QString &path, const QString &reason);

} // namespace speecher
