#include "providers/GptLiveProtocol.h"

#include "providers/CodexDictationClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace speecher {
namespace {

// intent and architecture pick the GPT-Live route; without them, or without
// openai-alpha, the call is refused with 400.
const QUrl kCallUrl(
    QStringLiteral("https://chatgpt.com/backend-api/codex/realtime/calls?intent=quicksilver&architecture=avas"));
const QString kSidebandBase = QStringLiteral("wss://api.openai.com/v1/live/");

void addAuthHeaders(QNetworkRequest &request, const QString &accessToken, const QString &accountId)
{
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setRawHeader("openai-alpha", "quicksilver=v2");
    // A workspace login may need it; a personal one does without.
    if (!accountId.isEmpty()) {
        request.setRawHeader("ChatGPT-Account-ID", accountId.toUtf8());
    }
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(codexBrowserUserAgent));
}

qint64 msField(const QJsonObject &object, const QString &key)
{
    return object.contains(key) ? qint64(object.value(key).toDouble()) : -1;
}

GptLiveEvent transcriptItem(GptLiveEvent::Kind kind, const QJsonObject &event)
{
    return {kind, event.value(QStringLiteral("item")).toObject().value(QStringLiteral("text")).toString(),
            msField(event, QStringLiteral("start_ms")), msField(event, QStringLiteral("end_ms"))};
}

// Windowed-sinc low-pass, cut off at 3.6 kHz of 16 kHz, Hamming window.
const std::array<float, 31> &lowPassTaps()
{
    static const std::array<float, 31> taps = [] {
        std::array<float, 31> result{};
        constexpr double cutoff = 3600.0 / 16000.0;
        const int middle = int(result.size()) / 2;
        double sum = 0.0;
        for (int index = 0; index < int(result.size()); ++index) {
            const int n = index - middle;
            const double sinc = n == 0 ? 2 * cutoff : std::sin(2 * std::numbers::pi * cutoff * n) / (std::numbers::pi * n);
            const double window = 0.54 - 0.46 * std::cos(2 * std::numbers::pi * index / (result.size() - 1));
            result[index] = float(sinc * window);
            sum += result[index];
        }
        for (float &tap : result) {
            tap = float(tap / sum);
        }
        return result;
    }();
    return taps;
}

quint8 muLaw(qint16 sample)
{
    constexpr int bias = 0x84;
    constexpr int clip = 32635;
    const int sign = sample < 0 ? 0x80 : 0;
    int magnitude = std::min(sample < 0 ? -int(sample) : int(sample), clip) + bias;
    int exponent = 7;
    for (int mask = 0x4000; (magnitude & mask) == 0 && exponent > 0; mask >>= 1) {
        --exponent;
    }
    const int mantissa = (magnitude >> (exponent + 3)) & 0x0F;
    return quint8(~(sign | (exponent << 4) | mantissa));
}

} // namespace

QStringList gptLiveVoices()
{
    return {QStringLiteral("juniper"), QStringLiteral("breeze"), QStringLiteral("cove"),
            QStringLiteral("ember"),   QStringLiteral("maple"),  QStringLiteral("arbor"),
            QStringLiteral("spruce"),  QStringLiteral("vale"),   QStringLiteral("sol")};
}

QNetworkRequest gptLiveCallRequest(const QString &accessToken, const QString &accountId)
{
    QNetworkRequest request(kCallUrl);
    addAuthHeaders(request, accessToken, accountId);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

QByteArray gptLiveCallBody(const QString &offerSdp, const GptLiveSessionOptions &options)
{
    QJsonObject session;
    if (!options.instructions.isEmpty()) {
        session.insert(QStringLiteral("instructions"), options.instructions);
    }
    if (!options.voice.isEmpty()) {
        session.insert(QStringLiteral("audio"),
                       QJsonObject{{QStringLiteral("output"), QJsonObject{{QStringLiteral("voice"), options.voice}}}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("sdp"), offerSdp}, {QStringLiteral("session"), session}})
        .toJson(QJsonDocument::Compact);
}

QString gptLiveCallId(const QByteArray &location)
{
    const QString path = QString::fromUtf8(location).section(QLatin1Char('?'), 0, 0);
    const QString last = path.section(QLatin1Char('/'), -1);
    return last.startsWith(QStringLiteral("rtc_")) ? last : QString();
}

QNetworkRequest gptLiveSidebandRequest(const QString &callId, const QString &accessToken, const QString &accountId)
{
    QNetworkRequest request(QUrl(kSidebandBase + callId));
    addAuthHeaders(request, accessToken, accountId);
    return request;
}

QByteArray gptLiveSessionClose()
{
    return QByteArrayLiteral(R"({"type":"session.close"})");
}

GptLiveEvent parseGptLiveEvent(const QByteArrayView message)
{
    using Kind = GptLiveEvent::Kind;
    const QJsonObject event = QJsonDocument::fromJson(message.toByteArray()).object();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("session.started")) {
        return {Kind::SessionStarted};
    }
    if (type == QStringLiteral("input_transcript.added")) {
        return transcriptItem(Kind::UserTranscriptDelta, event);
    }
    if (type == QStringLiteral("output_transcript.added")) {
        return transcriptItem(Kind::AssistantTranscriptDelta, event);
    }
    if (type == QStringLiteral("turn.done")) {
        const QJsonObject turn = event.value(QStringLiteral("turn")).toObject();
        const QString role = turn.value(QStringLiteral("role")).toString();
        if (role != QStringLiteral("user") && role != QStringLiteral("assistant")) {
            return {};
        }
        return {role == QStringLiteral("user") ? Kind::UserTurn : Kind::AssistantTurn,
                turn.value(QStringLiteral("transcript")).toString().trimmed(), msField(turn, QStringLiteral("start_ms")),
                msField(turn, QStringLiteral("end_ms"))};
    }
    if (type == QStringLiteral("session.output_audio.delta")) {
        GptLiveEvent audio{Kind::AssistantAudio};
        audio.pcm = QByteArray::fromBase64(event.value(QStringLiteral("delta")).toString().toLatin1());
        return audio;
    }
    if (type == QStringLiteral("session.usage.updated") || type == QStringLiteral("session.closed")) {
        GptLiveEvent usage{type == QStringLiteral("session.closed") ? Kind::SessionClosed : Kind::Usage};
        usage.text = event.value(QStringLiteral("reason")).toString();
        usage.audioMs = msField(event.value(QStringLiteral("usage")).toObject(), QStringLiteral("audio_duration_ms"));
        return usage;
    }
    if (type == QStringLiteral("error")) {
        const QJsonObject error = event.value(QStringLiteral("error")).toObject();
        return {Kind::Error, error.value(QStringLiteral("message")).toString()};
    }
    return {};
}

QByteArray pcmuFromPcm16(QByteArrayView pcm)
{
    QByteArray out(pcm.size() / 2, Qt::Uninitialized);
    for (qsizetype index = 0; index < out.size(); ++index) {
        out[index] = char(muLaw(qFromLittleEndian<qint16>(pcm.constData() + 2 * index)));
    }
    return out;
}

QByteArray Downsampler16kTo8k::process(QByteArrayView pcm16k)
{
    const std::array<float, kTaps> &taps = lowPassTaps();
    QByteArray out;
    out.reserve(pcm16k.size() / 2 + 2);
    for (qsizetype at = 0; at + 1 < pcm16k.size(); at += 2) {
        std::move(m_history.begin() + 1, m_history.end(), m_history.begin());
        m_history.back() = float(qFromLittleEndian<qint16>(pcm16k.constData() + at));
        // Every other sample.
        if (m_taken++ % 2 != 0) {
            continue;
        }
        float sum = 0.0f;
        for (int tap = 0; tap < kTaps; ++tap) {
            sum += taps[tap] * m_history[tap];
        }
        const qint16 sample = qint16(std::clamp(std::lround(sum), -32768L, 32767L));
        char bytes[2];
        qToLittleEndian(sample, bytes);
        out.append(bytes, 2);
    }
    return out;
}

} // namespace speecher
