#pragma once

#include "core/AppSettings.h"
#include "dictation/DictationPorts.h"
#include "providers/SpeechStream.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

namespace speecher {

class ProviderRegistry;
class TranscriptState;

// One input of a Recording streamed to a speech provider, with its own
// providers: each final the provider streams is the transcript as it arrives,
// so only speech providers that stream final text take the audio, and Codex
// does not transcribe it again at the end. Audio heard while the providers
// prepare waits for them, and a provider that fails before any reached it
// hands all of it to the next. Once the provider has connected, a dropped
// stream reconnects after a growing pause for as long as the input runs,
// renewing the sign-in first when it is due or was turned down, and up to ten
// minutes of audio waits for it. The caller marks where each utterance begins
// and ends.
class RecordingStream : public QObject {
    Q_OBJECT

public:
    explicit RecordingStream(ProviderRegistry *providers, QObject *parent = nullptr);
    ~RecordingStream() override;

    // From start() until finished().
    bool isRunning() const;
    // Streams what input hears until finishListening(), with settings' speech
    // provider and its fallbacks, which prepare once the input has started.
    // Not while running. An input that cannot start finishes the stream.
    void start(AudioInput *input, const AppSettings &settings);
    // Stops the input; what it heard is still sent, and the stream finishes
    // once the provider has answered it. listeningFinished() follows once the
    // input has stopped.
    void finishListening();
    // Has the speech provider begin an utterance with the last voicedBytes
    // the input heard, once the audio before has gone to it; see
    // SpeechTranscriber::beginUtterance.
    void beginUtterance(qsizetype voicedBytes);
    // Has the speech provider finalize the utterance the input has heard,
    // once that audio has gone to it; see SpeechTranscriber::endUtterance.
    // The input may have stopped, until its audio has all been sent.
    void endUtterance();
    // Stops at once; finished() comes with no error.
    void cancel();
    // How long the current speech provider may take to answer an utterance,
    // while running; see SpeechTranscriber::utteranceAnswerTimeoutMs.
    int utteranceAnswerTimeoutMs() const;

signals:
    // The speech provider accepted a stream: the first, and each reconnect.
    void connected();
    // The stream dropped, for reason; it reconnects after a pause.
    void reconnecting(const QString &reason);
    // The stream was down so long that the oldest unsent audio, this much of
    // it, was dropped.
    void audioLost(qint64 durationMs);
    // An utterance the speech provider did not transcribe, or only in part,
    // for reason; the stream goes on.
    void utteranceFailed(const QString &reason);
    // Each piece of text that will not change again, in order: a final the
    // provider sent, or the partial a stream left when it ended.
    void textFinalized(const QString &text);
    // After finishListening(): the input has stopped, and all it heard is in
    // the stream.
    void listeningFinished();
    // The stream ended: stopped and answered, cancelled, or on its own. error
    // says why it ended on its own, or what went wrong on the way (the input
    // failed after it heard something); empty otherwise.
    void finished(const QString &error);

private:
    enum class Input { Off, Starting, Listening, Stopping };

    bool reconnectLater(const SpeechFailure &failure);
    void waitToReconnect(const QString &reason);
    void reconnect();
    bool renewSignIn();
    void handleSignInRenewed(const std::optional<ProviderFailure> &failure);
    void endInput();
    void fail(const QString &message);
    void finish();
    void release();

    TranscriptState *m_transcript;
    // The recording's speech provider, then the saved fallbacks.
    SpeechStream *m_stream;
    // Runs while the stream waits to reconnect.
    QTimer m_reconnectTimer;
    // Which of the growing pauses before a reconnect comes next.
    int m_reconnectDelayIndex = 0;
    bool m_running = false;
    QString m_error;

    QPointer<AudioInput> m_input;
    Input m_inputState = Input::Off;
    // What the providers prepare with once the input has started.
    AppSettings m_settings;
    // The service turned the sign-in down since a stream last connected.
    bool m_signInRefused = false;
};

} // namespace speecher
