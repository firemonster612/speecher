#include "platform/audio/PulseSystemAudioInput.h"

#include "recording/SystemAudioPresentation.h"

#include <QMetaObject>

#include <pulse/pulseaudio.h>

namespace speecher {
namespace {

constexpr pa_sample_spec kSampleSpec{PA_SAMPLE_S16LE, 16000, 1};
// 100 ms of it, how much the server is asked to deliver at a time.
constexpr uint32_t kFragmentBytes = 16000 * 2 / 10;

class MainloopLocker {
public:
    explicit MainloopLocker(pa_threaded_mainloop *mainloop)
        : m_mainloop(mainloop)
    {
        pa_threaded_mainloop_lock(m_mainloop);
    }
    ~MainloopLocker() { pa_threaded_mainloop_unlock(m_mainloop); }
    Q_DISABLE_COPY_MOVE(MainloopLocker)

private:
    pa_threaded_mainloop *m_mainloop;
};

QString pulseError(pa_context *context)
{
    return QString::fromUtf8(pa_strerror(pa_context_errno(context)));
}

QString lostConnection(pa_context *context)
{
    return QStringLiteral("Lost the connection to the sound server: %1").arg(pulseError(context));
}

// A stream also fails when the connection under it is lost, which is the
// error worth reporting then.
QString captureError(pa_context *context)
{
    if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(context))) {
        return lostConnection(context);
    }
    return systemAudioCaptureFailedText(pulseError(context));
}

} // namespace

PulseSystemAudioInput::PulseSystemAudioInput(QObject *parent)
    : AudioInput(parent)
{
}

PulseSystemAudioInput::~PulseSystemAudioInput()
{
    stop();
}

bool PulseSystemAudioInput::start(QString *error)
{
    if (m_mainloop) {
        return true;
    }
    QString message;
    if (open(&message)) {
        return true;
    }
    stop();
    if (error) {
        *error = message;
    }
    return false;
}

bool PulseSystemAudioInput::open(QString *error)
{
    m_mainloop = pa_threaded_mainloop_new();
    m_context = pa_context_new(pa_threaded_mainloop_get_api(m_mainloop), "Speecher");
    pa_context_set_state_callback(
        m_context,
        [](pa_context *context, void *self) {
            auto *input = static_cast<PulseSystemAudioInput *>(self);
            pa_threaded_mainloop_signal(input->m_mainloop, 0);
            if (pa_context_get_state(context) == PA_CONTEXT_FAILED) {
                input->postFailure(lostConnection(context));
            }
        },
        this);
    if (pa_threaded_mainloop_start(m_mainloop) < 0) {
        *error = QStringLiteral("Could not start the sound server connection.");
        return false;
    }

    MainloopLocker lock(m_mainloop);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        *error = QStringLiteral("Could not connect to the sound server: %1").arg(pulseError(m_context));
        return false;
    }
    for (pa_context_state_t state; (state = pa_context_get_state(m_context)) != PA_CONTEXT_READY;) {
        if (!PA_CONTEXT_IS_GOOD(state)) {
            *error = QStringLiteral("Could not connect to the sound server: %1").arg(pulseError(m_context));
            return false;
        }
        pa_threaded_mainloop_wait(m_mainloop);
    }

    // A server change event is the only sign the default output moved. The
    // server answers requests in order, so asking for the default output only
    // after subscribing means a change during start is either in the answer
    // or reported after it.
    pa_context_set_subscribe_callback(
        m_context,
        [](pa_context *, pa_subscription_event_type_t, uint32_t, void *self) {
            if (pa_operation *query = static_cast<PulseSystemAudioInput *>(self)->queryDefaultSink()) {
                pa_operation_unref(query);
            }
        },
        this);
    pa_operation *subscription = pa_context_subscribe(m_context, PA_SUBSCRIPTION_MASK_SERVER, nullptr, nullptr);
    if (!subscription) {
        *error = defaultSoundOutputUnfollowedText(pulseError(m_context));
        return false;
    }
    pa_operation_unref(subscription);

    if (pa_operation *query = queryDefaultSink()) {
        while (pa_operation_get_state(query) == PA_OPERATION_RUNNING) {
            pa_threaded_mainloop_wait(m_mainloop);
        }
        pa_operation_unref(query);
    }
    if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(m_context))) {
        *error = lostConnection(m_context);
        return false;
    }
    if (m_sinkName.isEmpty()) {
        *error = noSoundOutputText();
        return false;
    }
    if (!connectStream(m_sinkName)) {
        *error = captureError(m_context);
        return false;
    }
    // A default output change can replace the stream while this waits.
    for (pa_stream_state_t state;
         (state = m_stream ? pa_stream_get_state(m_stream) : PA_STREAM_FAILED) != PA_STREAM_READY;) {
        if (!PA_STREAM_IS_GOOD(state)) {
            *error = captureError(m_context);
            return false;
        }
        pa_threaded_mainloop_wait(m_mainloop);
    }
    return true;
}

void PulseSystemAudioInput::stop()
{
    if (!m_mainloop) {
        return;
    }
    // Once the PulseAudio thread has exited nothing below races it.
    pa_threaded_mainloop_stop(m_mainloop);
    disconnectStream();
    pa_context_set_state_callback(m_context, nullptr, nullptr);
    pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
    pa_context_disconnect(m_context);
    pa_context_unref(m_context);
    m_context = nullptr;
    pa_threaded_mainloop_free(m_mainloop);
    m_mainloop = nullptr;
    m_sinkName.clear();
    ++m_generation;
}

bool PulseSystemAudioInput::isActive() const
{
    return m_mainloop != nullptr;
}

// Every answer, to start's request or to a change event, goes through
// followDefaultSink in the order the server sent them.
pa_operation *PulseSystemAudioInput::queryDefaultSink()
{
    return pa_context_get_server_info(
        m_context,
        [](pa_context *, const pa_server_info *info, void *self) {
            auto *input = static_cast<PulseSystemAudioInput *>(self);
            if (info && info->default_sink_name) {
                input->followDefaultSink(info->default_sink_name);
            }
            pa_threaded_mainloop_signal(input->m_mainloop, 0);
        },
        this);
}

bool PulseSystemAudioInput::connectStream(const QByteArray &sinkName)
{
    ++m_streamGeneration;
    m_stream = pa_stream_new(m_context, "System audio", &kSampleSpec, nullptr);
    if (!m_stream) {
        return false;
    }
    pa_stream_set_state_callback(
        m_stream,
        [](pa_stream *stream, void *self) {
            auto *input = static_cast<PulseSystemAudioInput *>(self);
            pa_threaded_mainloop_signal(input->m_mainloop, 0);
            if (pa_stream_get_state(stream) == PA_STREAM_FAILED) {
                input->postStreamFailure(systemAudioCaptureStoppedText(pulseError(input->m_context)));
            }
        },
        this);
    pa_stream_set_read_callback(
        m_stream,
        [](pa_stream *stream, size_t, void *self) {
            static_cast<PulseSystemAudioInput *>(self)->readStream(stream);
        },
        this);
    pa_buffer_attr buffer;
    buffer.maxlength = buffer.tlength = buffer.prebuf = buffer.minreq = uint32_t(-1);
    buffer.fragsize = kFragmentBytes;
    // PulseAudio and pipewire-pulse both name a sink's monitor source this way.
    const QByteArray monitor = sinkName + ".monitor";
    return pa_stream_connect_record(m_stream, monitor.constData(), &buffer, PA_STREAM_ADJUST_LATENCY) == 0;
}

void PulseSystemAudioInput::disconnectStream()
{
    if (!m_stream) {
        return;
    }
    pa_stream_set_read_callback(m_stream, nullptr, nullptr);
    if (pa_stream_get_state(m_stream) == PA_STREAM_CREATING) {
        // libpulse cannot disconnect a stream the server has not finished
        // creating, as when the default output changes twice in a row, and
        // the context keeps it alive, so it disconnects itself once ready.
        pa_stream_set_state_callback(
            m_stream,
            [](pa_stream *stream, void *) {
                if (pa_stream_get_state(stream) == PA_STREAM_READY) {
                    pa_stream_disconnect(stream);
                }
            },
            nullptr);
    } else {
        pa_stream_set_state_callback(m_stream, nullptr, nullptr);
        pa_stream_disconnect(m_stream);
    }
    pa_stream_unref(m_stream);
    m_stream = nullptr;
}

void PulseSystemAudioInput::followDefaultSink(const QByteArray &sinkName)
{
    if (sinkName == m_sinkName) {
        return;
    }
    m_sinkName = sinkName;
    // Until start connects the first stream, it connects to the latest default,
    // and once a stream failure has ended capture there is nothing to replace.
    if (!m_stream) {
        return;
    }
    disconnectStream();
    if (!connectStream(sinkName)) {
        postStreamFailure(systemAudioCaptureFailedText(pulseError(m_context)));
    }
}

void PulseSystemAudioInput::readStream(pa_stream *stream)
{
    QByteArray pcm;
    while (pa_stream_readable_size(stream) > 0) {
        const void *data = nullptr;
        size_t size = 0;
        if (pa_stream_peek(stream, &data, &size) < 0) {
            postStreamFailure(QStringLiteral("Could not read system audio: %1").arg(pulseError(m_context)));
            return;
        }
        if (size == 0) {
            break;
        }
        // No data with a size is a hole in the stream, which is silence.
        if (data) {
            pcm.append(static_cast<const char *>(data), qsizetype(size));
        } else {
            pcm.append(qsizetype(size), '\0');
        }
        pa_stream_drop(stream);
    }
    if (pcm.isEmpty()) {
        return;
    }
    const quint64 generation = m_generation;
    QMetaObject::invokeMethod(
        this,
        [this, pcm, generation] {
            if (generation == m_generation) {
                emit audioChunk(pcm);
            }
        },
        Qt::QueuedConnection);
}

void PulseSystemAudioInput::postFailure(const QString &message)
{
    const quint64 generation = m_generation;
    QMetaObject::invokeMethod(this, [this, message, generation] { fail(generation, message); }, Qt::QueuedConnection);
}

void PulseSystemAudioInput::postStreamFailure(const QString &message)
{
    const quint64 generation = m_generation;
    const quint64 streamGeneration = m_streamGeneration;
    QMetaObject::invokeMethod(
        this,
        [this, message, generation, streamGeneration] {
            if (generation != m_generation) {
                return;
            }
            {
                // Decided under the lock, and the stream dropped before it is
                // released, so a default output change cannot start a
                // replacement that stop() would then tear down.
                MainloopLocker lock(m_mainloop);
                if (streamGeneration != m_streamGeneration) {
                    return;
                }
                disconnectStream();
            }
            fail(generation, message);
        },
        Qt::QueuedConnection);
}

void PulseSystemAudioInput::fail(quint64 generation, const QString &message)
{
    if (generation != m_generation) {
        return;
    }
    stop();
    emit failed(message);
}

} // namespace speecher
