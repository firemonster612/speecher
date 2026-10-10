#include "platform/mac/MacMicrophoneInput.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QTimer>

namespace speecher {

MacMicrophoneInput::MacMicrophoneInput(CaptureFactory createCapture, int postRollMs, QObject *parent)
    : AudioInput(parent)
    , m_createCapture(std::move(createCapture))
    , m_postRollMs(postRollMs)
{
}

MacMicrophoneInput::~MacMicrophoneInput()
{
    endCapture();
}

bool MacMicrophoneInput::start(QString *error)
{
    if (m_capture) {
        return true;
    }
    std::unique_ptr<MacMicrophoneCapture> capture = m_createCapture();
    const quint64 generation = m_generation;
    const QString failure = capture->open([this](QByteArrayView pcm) { queueAudio(pcm); },
                                          [this, generation](const QString &message) {
                                              QMetaObject::invokeMethod(
                                                  this,
                                                  [this, generation, message] { fail(generation, message); },
                                                  Qt::QueuedConnection);
                                          });
    if (!failure.isEmpty()) {
        if (error) {
            *error = failure;
        }
        return false;
    }
    m_converter.reset(capture->format());
    m_echoCancellationWarning = capture->echoCancellationWarning();
    m_capture = std::move(capture);
    return true;
}

void MacMicrophoneInput::stop()
{
    if (m_capture && m_postRollMs > 0) {
        // The post-roll's audio is delivered meanwhile.
        QEventLoop loop;
        QTimer::singleShot(m_postRollMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    endCapture();
    deliverAudio();
}

bool MacMicrophoneInput::isActive() const
{
    return m_capture != nullptr;
}

QString MacMicrophoneInput::echoCancellationWarning() const
{
    return m_echoCancellationWarning;
}

void MacMicrophoneInput::queueAudio(QByteArrayView pcm)
{
    {
        const QMutexLocker locker(&m_mutex);
        m_queued.append(pcm.toByteArray());
    }
    QMetaObject::invokeMethod(this, &MacMicrophoneInput::deliverAudio, Qt::QueuedConnection);
}

// One chunk at a time, so a stop from a handler finds the rest still queued
// and delivers it, in order, before it returns. What is queued after that
// stop is a later capture's, which has a delivery of its own.
void MacMicrophoneInput::deliverAudio()
{
    const quint64 generation = m_generation;
    while (generation == m_generation) {
        QByteArray chunk;
        {
            const QMutexLocker locker(&m_mutex);
            if (m_queued.isEmpty()) {
                return;
            }
            chunk = m_queued.takeFirst();
        }
        const AudioPcmConversion conversion = m_converter.convert(chunk);
        if (!conversion.error.isEmpty()) {
            fail(generation, conversion.error);
            return;
        }
        if (!conversion.pcm16Mono16k.isEmpty()) {
            emit audioChunk(conversion.pcm16Mono16k);
        }
    }
}

// Destroying the capture stops its thread handing over audio.
void MacMicrophoneInput::endCapture()
{
    if (!m_capture) {
        return;
    }
    m_capture.reset();
    ++m_generation;
}

void MacMicrophoneInput::fail(quint64 generation, const QString &message)
{
    if (generation != m_generation || !m_capture) {
        return;
    }
    endCapture();
    emit failed(message);
}

} // namespace speecher
