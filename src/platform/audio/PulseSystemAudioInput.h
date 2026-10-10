#pragma once

#include "dictation/DictationPorts.h"

#include <QByteArray>

struct pa_context;
struct pa_operation;
struct pa_stream;
struct pa_threaded_mainloop;

namespace speecher {

// What the speakers play, read from the default output's monitor source as
// 16 kHz mono s16 like the microphone, and moved to the new default output's
// monitor when that changes. It speaks the PulseAudio protocol, which
// PipeWire serves through pipewire-pulse, because neither of Qt's audio
// backends lists monitor sources and Qt Multimedia already links libpulse.
class PulseSystemAudioInput final : public AudioInput {
public:
    explicit PulseSystemAudioInput(QObject *parent = nullptr);
    ~PulseSystemAudioInput() override;

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

private:
    // Leaves whatever it got as far as for stop() to tear down when it fails.
    bool open(QString *error);
    // These run with the main loop locked.
    pa_operation *queryDefaultSink();
    bool connectStream(const QByteArray &sinkName);
    void disconnectStream();
    void followDefaultSink(const QByteArray &sinkName);
    void readStream(pa_stream *stream);
    // A failure of the connection, or of the stream, which is dropped instead
    // if the stream has been replaced by the time it reaches the main thread.
    void postFailure(const QString &message);
    void postStreamFailure(const QString &message);
    // Runs on the main thread.
    void fail(quint64 generation, const QString &message);

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    pa_stream *m_stream = nullptr;
    // The default output as last reported, whose monitor the stream reads.
    QByteArray m_sinkName;
    // Bumped by every stop, so audio and failures the PulseAudio thread queued
    // before it are dropped rather than reaching the next start.
    quint64 m_generation = 0;
    // Bumped by every new stream.
    quint64 m_streamGeneration = 0;
};

} // namespace speecher
