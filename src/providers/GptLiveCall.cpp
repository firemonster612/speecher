#include "providers/GptLiveCall.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <rtc/rtc.hpp>

#include <chrono>

namespace speecher {
namespace {

// PCMU, 20 ms a packet.
constexpr int kPcmuRate = 8000;
constexpr int kFrameMs = 20;
constexpr qsizetype kFrameBytes = kPcmuRate * kFrameMs / 1000;
// The payload type the server answers PCMU with.
constexpr int kPcmuPayloadType = 0;
// A bad session object fails silently once the call is created: no
// session.started ever comes.
constexpr int kStartTimeoutMs = 20000;
// How long a goodbye may take before the call just ends.
constexpr int kCloseTimeoutMs = 5000;
// How often a slow session is hung up on for another.
constexpr int kMostRedials = 3;
// The echoes that span 2 s of audio, and how long they may take to come.
constexpr int kProbeEchoes = 11;
constexpr int kProbeTimeoutMs = 4000;
// Silence in PCMU.
constexpr char kPcmuSilence = char(0xFF);

// Runs work on the Qt thread for call, if call is still there:
// libdatachannel calls back on threads of its own.
void onQtThread(const QPointer<GptLiveCall> &call, std::function<void(GptLiveCall *)> work)
{
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [call, work = std::move(work)] {
            if (call) {
                work(call);
            }
        },
        Qt::QueuedConnection);
}

QString refusal(QNetworkReply *reply)
{
    const QString message = QJsonDocument::fromJson(reply->readAll())
                                .object()
                                .value(QStringLiteral("error"))
                                .toObject()
                                .value(QStringLiteral("message"))
                                .toString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString why = !message.isEmpty() ? message : status ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
    return QStringLiteral("GPT-Live refused the call: %1").arg(why);
}

} // namespace

GptLiveCall::GptLiveCall(QObject *parent)
    : GptLiveSession(parent)
{
    m_pacer.setInterval(kFrameMs);
    m_pacer.setTimerType(Qt::PreciseTimer);
    connect(&m_pacer, &QTimer::timeout, this, &GptLiveCall::sendFrames);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (m_closing) {
            finish();
        } else {
            fail(QStringLiteral("GPT-Live did not start the session"));
        }
    });
    if (const QString trace = qEnvironmentVariable("SPEECHER_GPT_LIVE_TRACE"); !trace.isEmpty()) {
        m_trace.setFileName(trace);
        if (!m_trace.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning().noquote() << "gpt-live trace cannot be written path=" + trace;
        }
        m_traceClock.start();
    }
    m_probeDeadline.setSingleShot(true);
    connect(&m_probeDeadline, &QTimer::timeout, this, [this] { judgeSpeed(true); });
    connect(&m_sideband, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        traceMessage("in", message.toUtf8());
        const GptLiveEvent event = parseGptLiveEvent(message.toUtf8());
        using Kind = GptLiveEvent::Kind;
        if (event.kind == Kind::SessionStarted) {
            // Audio, silence until the caller's comes, flows from here on.
            m_started = true;
            if (m_options.acceptSlowSession) {
                judgeSpeed(false);
            } else {
                m_probeDeadline.start(kProbeTimeoutMs);
            }
            return;
        }
        if (event.kind == Kind::InputEcho && !m_ready) {
            takeEcho();
            return;
        }
        if (!m_ready && event.kind != Kind::SessionClosed && event.kind != Kind::Error) {
            return;
        }
        emit this->event(event);
        if (event.kind == Kind::SessionClosed) {
            finish();
        }
    });
    connect(&m_sideband, &QWebSocket::disconnected, this, [this] {
        if (m_hangingUp) {
            return;
        }
        qInfo().noquote() << QStringLiteral("gpt-live sideband closed code=%1 reason=%2 closing=%3")
                                 .arg(int(m_sideband.closeCode()))
                                 .arg(m_sideband.closeReason())
                                 .arg(m_closing);
        traceMessage("closed", QByteArray::number(int(m_sideband.closeCode())) + ' ' + m_sideband.closeReason().toUtf8());
        if (!m_closing) {
            fail(QStringLiteral("GPT-Live hung up"));
            return;
        }
        finish();
    });
}

GptLiveCall::~GptLiveCall()
{
    m_pacer.stop();
    if (m_peer) {
        m_peer->close();
    }
}

void GptLiveCall::start(const QString &accessToken, const QString &accountId, const GptLiveSessionOptions &options)
{
    m_accessToken = accessToken;
    m_accountId = accountId;
    m_options = options;
    dial();
}

void GptLiveCall::dial()
{
    const quint64 dial = ++m_dial;
    m_deadline.start(kStartTimeoutMs);
    m_peer = std::make_shared<rtc::PeerConnection>(rtc::Configuration{});
    const uint32_t ssrc = QRandomGenerator::global()->generate();
    rtc::Description::Audio audio("audio", rtc::Description::Direction::SendRecv);
    audio.addPCMUCodec(kPcmuPayloadType);
    audio.addSSRC(ssrc, std::string("speecher"));
    m_track = m_peer->addTrack(audio);
    auto config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "speecher", kPcmuPayloadType, kPcmuRate);
    m_track->setMediaHandler(std::make_shared<rtc::PCMURtpPacketizer>(config));
    m_track->chainMediaHandler(std::make_shared<rtc::RtcpSrReporter>(config));

    const QPointer<GptLiveCall> self(this);
    // The server takes the offer once, so it waits for every candidate.
    m_peer->onGatheringStateChange([self, dial, peer = std::weak_ptr(m_peer)](rtc::PeerConnection::GatheringState state) {
        if (state != rtc::PeerConnection::GatheringState::Complete) {
            return;
        }
        const std::shared_ptr<rtc::PeerConnection> locked = peer.lock();
        const std::optional<rtc::Description> offer = locked ? locked->localDescription() : std::nullopt;
        if (offer) {
            const QString sdp = QString::fromStdString(std::string(*offer));
            onQtThread(self, [dial, sdp](GptLiveCall *call) { call->offerReady(dial, sdp); });
        }
    });
    m_peer->onStateChange([self, dial, peer = std::weak_ptr(m_peer)](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) {
            const std::shared_ptr<rtc::PeerConnection> locked = peer.lock();
            const QByteArray remote = locked ? QByteArray::fromStdString(locked->remoteAddress().value_or("")) : QByteArray();
            onQtThread(self, [remote](GptLiveCall *call) { call->traceMessage("connected", remote); });
        }
        if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Disconnected) {
            onQtThread(self, [dial](GptLiveCall *call) {
                if (!call->m_closing && call->m_dial == dial) {
                    call->fail(QStringLiteral("The call's connection to GPT-Live failed"));
                }
            });
        }
    });
    m_peer->setLocalDescription(rtc::Description::Type::Offer);
}

void GptLiveCall::offerReady(quint64 dial, const QString &sdp)
{
    if (m_finished || m_closing || dial != m_dial) {
        return;
    }
    QNetworkReply *reply = m_network.post(gptLiveCallRequest(m_accessToken, m_accountId), gptLiveCallBody(sdp, m_options));
    connect(reply, &QNetworkReply::finished, this, [this, reply, dial] {
        reply->deleteLater();
        if (dial == m_dial) {
            callCreated(reply);
        }
    });
}

void GptLiveCall::callCreated(QNetworkReply *reply)
{
    if (m_finished || m_closing) {
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    traceMessage("call", QByteArray::number(status));
    if (status != 201) {
        fail(refusal(reply));
        return;
    }
    const QString callId = gptLiveCallId(reply->rawHeader("Location"));
    const QByteArray answer = reply->readAll();
    if (callId.isEmpty() || answer.isEmpty()) {
        fail(QStringLiteral("GPT-Live answered the call without one"));
        return;
    }
    m_peer->setRemoteDescription(rtc::Description(answer.toStdString(), rtc::Description::Type::Answer));
    m_sideband.open(gptLiveSidebandRequest(callId, m_accessToken, m_accountId));
    m_pacer.start();
}

void GptLiveCall::takeEcho()
{
    if (m_probeEchoes++ == 0) {
        m_probe.start();
    }
    if (m_probeEchoes == kProbeEchoes) {
        // A debugging aid: the first n sessions count as slow, to try the
        // redial out.
        const bool forced = m_redials < qEnvironmentVariableIntValue("SPEECHER_GPT_LIVE_FORCE_SLOW");
        judgeSpeed(forced || gptLiveSessionIsSlow(m_probe.elapsed()));
    }
}

void GptLiveCall::judgeSpeed(bool slow)
{
    m_probeDeadline.stop();
    if (m_ready || m_closing) {
        return;
    }
    traceMessage("speed", (slow ? QByteArray("slow") : QByteArray("real time")) + " echo_ms=" + QByteArray::number(m_probe.isValid() ? m_probe.elapsed() : -1));
    if (slow && m_redials < kMostRedials) {
        ++m_redials;
        emit event({GptLiveEvent::Kind::Error,
                    QStringLiteral("GPT-Live is running this session at half speed; calling again (%1 of %2)")
                        .arg(m_redials)
                        .arg(kMostRedials)});
        // Not from inside the sideband's own signal, which hanging up aborts.
        QTimer::singleShot(0, this, [this, dial = m_dial] {
            if (dial == m_dial && !m_closing && !m_finished) {
                hangUpForRedial();
                this->dial();
            }
        });
        return;
    }
    if (slow) {
        emit event({GptLiveEvent::Kind::Error,
                    QStringLiteral("GPT-Live is running this session at half speed; going on with it")});
    }
    m_ready = true;
    m_deadline.stop();
    emit event({GptLiveEvent::Kind::SessionStarted});
}

void GptLiveCall::hangUpForRedial()
{
    m_hangingUp = true;
    if (m_sideband.state() == QAbstractSocket::ConnectedState) {
        m_sideband.sendTextMessage(QString::fromLatin1(gptLiveSessionClose()));
    }
    m_sideband.abort();
    m_hangingUp = false;
    m_pacer.stop();
    if (m_peer) {
        m_peer->close();
    }
    m_peer.reset();
    m_track.reset();
    m_started = false;
    m_since.invalidate();
    m_framesSent = 0;
    m_probeEchoes = 0;
}

void GptLiveCall::sendControl(const QByteArray &message)
{
    if (m_sideband.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    traceMessage("out", message);
    m_sideband.sendTextMessage(QString::fromUtf8(message));
}

void GptLiveCall::sendContext(const QString &text)
{
    sendControl(gptLiveContextAppend(text));
}

void GptLiveCall::setInputPaused(bool paused)
{
    sendControl(gptLiveInputPause(paused));
}

void GptLiveCall::sendAudio(const QByteArray &pcm16k)
{
    m_pcmu += pcmuFromPcm16(m_downsampler.process(pcm16k));
}

qint64 GptLiveCall::queuedMs() const
{
    return m_pcmu.size() * 1000 / kPcmuRate;
}

// Every frame due since the track opened, the caller's audio first, then
// silence.
void GptLiveCall::sendFrames()
{
    if (!m_track || !m_track->isOpen() || !m_started) {
        return;
    }
    if (!m_since.isValid()) {
        m_since.start();
    }
    const qint64 due = m_since.elapsed() / kFrameMs + 1;
    for (; m_framesSent < due; ++m_framesSent) {
        QByteArray frame = m_pcmu.left(kFrameBytes);
        m_pcmu.remove(0, frame.size());
        frame.append(kFrameBytes - frame.size(), kPcmuSilence);
        const std::chrono::duration<double> at(double(m_framesSent) * kFrameMs / 1000.0);
        try {
            m_track->sendFrame(reinterpret_cast<const rtc::byte *>(frame.constData()), size_t(frame.size()),
                               rtc::FrameInfo(at));
        } catch (const std::exception &error) {
            fail(QStringLiteral("Could not send the audio: %1").arg(QString::fromUtf8(error.what())));
            return;
        }
        if (m_framesSent % 50 == 0) {
            traceMessage("rtp", QStringLiteral("frames=%1 since_open_ms=%2 queued_ms=%3")
                                    .arg(m_framesSent)
                                    .arg(m_since.elapsed())
                                    .arg(queuedMs())
                                    .toUtf8());
        }
    }
}

void GptLiveCall::close()
{
    if (m_closing || m_finished) {
        return;
    }
    m_closing = true;
    if (m_sideband.state() != QAbstractSocket::ConnectedState) {
        finish();
        return;
    }
    traceMessage("out", gptLiveSessionClose());
    m_sideband.sendTextMessage(QString::fromLatin1(gptLiveSessionClose()));
    m_deadline.start(kCloseTimeoutMs);
}

void GptLiveCall::traceMessage(const char *direction, const QByteArray &message)
{
    if (!m_trace.isOpen()) {
        return;
    }
    static const QRegularExpression audio(QStringLiteral(R"re("(audio|delta)":"[A-Za-z0-9+/=]{200,}")re"));
    QString line = QString::fromUtf8(message);
    line.replace(audio, QStringLiteral(R"re("\1":"<audio>")re"));
    m_trace.write(QStringLiteral("%1 %2 %3\n").arg(m_traceClock.elapsed()).arg(QLatin1String(direction), line).toUtf8());
    m_trace.flush();
}

void GptLiveCall::fail(const QString &message)
{
    if (m_finished) {
        return;
    }
    emit failed(message);
    m_closing = true;
    finish();
}

void GptLiveCall::finish()
{
    if (std::exchange(m_finished, true)) {
        return;
    }
    m_pacer.stop();
    m_deadline.stop();
    m_probeDeadline.stop();
    m_sideband.abort();
    if (m_peer) {
        m_peer->close();
    }
    emit closed();
}

} // namespace speecher
