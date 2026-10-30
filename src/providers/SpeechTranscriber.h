#pragma once

#include "core/AppSettings.h"
#include "core/ProviderFailure.h"
#include "providers/PreparationJob.h"

#include <QList>
#include <QObject>

#include <optional>

namespace speecher {

// What every speech provider implements: it turns an attempt's audio into the
// Raw Transcript, and says why when it can't.

struct SpeechFailure {
    quint64 attemptId = 0;
    QString message;
    bool retryable = false;
    SpeechPhase phase = SpeechPhase::Connect;
    ProviderFailureKind kind = ProviderFailureKind::Other;
    int httpStatus = 0;
};

// A stretch of the Raw Transcript and when it was spoken, in milliseconds from
// the start of the attempt's audio.
struct TranscriptSegment {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;

    bool operator==(const TranscriptSegment &) const = default;
};

class SpeechTranscriber : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // The sign-in methods take the accounts a provider that signs in reads
    // its credentials from.
    virtual bool requiresRefresh(const SpeechSettings &settings, const AccountSettings &accounts) const = 0;
    virtual std::optional<PreparationJob> createPrepareJob(const SpeechSettings &settings,
                                                           const AccountSettings &accounts)
    {
        Q_UNUSED(settings);
        Q_UNUSED(accounts);
        return std::nullopt;
    }
    // A failure sends a Dictation Session on to the next provider whatever its
    // kind, so one the provider cannot classify is Unavailable.
    virtual std::optional<ProviderFailure> prepare(const SpeechSettings &settings,
                                                   const AccountSettings &accounts) = 0;
    // Whether prepare() renews a sign-in that expires, so one the service
    // turned down may be taken once it has run again. A key from Settings
    // is not renewed.
    virtual bool renewsSignIn() const { return false; }
    // How long the provider may take to answer an utterance once it has all
    // of it; a recording's stop waits that long for each one left.
    virtual int utteranceAnswerTimeoutMs() const { return 15000; }
    // Whether finalTranscript arrives while the audio still streams, an
    // utterance at a time, by itself or at each endUtterance(), rather than
    // all at the end. A recording needs it.
    virtual bool streamsFinalText(const SpeechSettings &settings) const
    {
        Q_UNUSED(settings);
        return false;
    }
    virtual void startAttempt(quint64 attemptId, const SpeechSettings &settings) = 0;
    virtual void sendAudio(quint64 attemptId, const QByteArray &pcm) = 0;
    virtual void finishInput(quint64 attemptId) = 0;
    virtual void cancelAttempt(quint64 attemptId) = 0;
    // Finalizes the utterance heard so far while the stream carries on, for a
    // provider that does not end one at a silence by itself. Providers that do
    // ignore it.
    virtual void endUtterance(quint64 attemptId) { Q_UNUSED(attemptId) }
    // With SpeechSettings::cutIntoUtterances, the audio from here on is the
    // next utterance, and what came since the last ended was quiet. Providers
    // that take the quiet too ignore it.
    virtual void beginUtterance(quint64 attemptId) { Q_UNUSED(attemptId) }

signals:
    // Optional: the service accepted the attempt's stream, so audio sent from
    // here on reaches it. Text from the attempt implies it.
    void attemptConnected(quint64 attemptId);
    void partialTranscript(quint64 attemptId, const QString &text);
    void finalTranscript(quint64 attemptId, const QString &text);
    // Optional: a whole-attempt transcript that supersedes the accumulated
    // finals, emitted before attemptCompleted by providers that retranscribe
    // the buffered audio in one pass at the end.
    void attemptTranscript(quint64 attemptId, const QString &text);
    // Optional: when each part of that whole-attempt transcript was spoken,
    // emitted just before it by providers whose service returns timings.
    void attemptSegments(quint64 attemptId, const QList<speecher::TranscriptSegment> &segments);
    void attemptCompleted(quint64 attemptId);
    void failed(const speecher::SpeechFailure &failure);
    // Optional: an utterance endUtterance() ended was not transcribed, or
    // only in part, and the attempt goes on with the next.
    void utteranceFailed(const speecher::SpeechFailure &failure);
};

} // namespace speecher
