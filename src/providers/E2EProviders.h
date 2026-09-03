#pragma once

class QObject;

namespace speecher {

class SpeechTranscriber;
class TranscriptRefiner;

SpeechTranscriber *createE2ESpeechTranscriber(QObject *parent);
TranscriptRefiner *createE2ETranscriptRefiner(QObject *parent);

} // namespace speecher
