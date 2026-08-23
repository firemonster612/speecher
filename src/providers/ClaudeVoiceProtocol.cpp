#include "providers/ClaudeVoiceProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

namespace speecher {
namespace {

constexpr qsizetype kMaximumKeytermBytes = 1024;

QString redactedErrorSummary(const QJsonObject &object)
{
    const QJsonValue errorValue = object.value(QStringLiteral("error"));
    QJsonObject error = errorValue.toObject();
    if (error.isEmpty() && errorValue.isString()) {
        return errorValue.toString().left(240);
    }
    QStringList parts;
    for (const QString &key : {QStringLiteral("type"), QStringLiteral("code"), QStringLiteral("error_code"), QStringLiteral("message"), QStringLiteral("description")}) {
        const QString value = error.value(key).toString(object.value(key).toString());
        if (!value.isEmpty()) {
            parts << key + QStringLiteral("=") + value.left(240);
        }
    }
    return parts.join(QStringLiteral(" "));
}

bool envFlag(const char *name, bool defaultValue)
{
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    if (value.isEmpty()) {
        return defaultValue;
    }
    if (value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("yes") || value == QStringLiteral("on")) {
        return true;
    }
    if (value == QStringLiteral("0") || value == QStringLiteral("false") || value == QStringLiteral("no") || value == QStringLiteral("off")) {
        return false;
    }
    return defaultValue;
}

bool typedInterimsEnabled()
{
    if (envFlag("CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED", false)) {
        return true;
    }
    return envFlag("SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED", true);
}

} // namespace

ClaudeVoiceEvent parseClaudeVoiceEvent(const QString &message)
{
    const QJsonObject object = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("TranscriptInterim") || type == QStringLiteral("TranscriptText")) {
        return {ClaudeVoiceEventKind::Working, object.value(QStringLiteral("data")).toString(), {}};
    }
    if (type == QStringLiteral("TranscriptEndpoint")) {
        return {ClaudeVoiceEventKind::Endpoint, object.value(QStringLiteral("data")).toString(), {}};
    }
    if (type == QStringLiteral("TranscriptError")) {
        return {ClaudeVoiceEventKind::TranscriptError, {}, redactedErrorSummary(object)};
    }
    if (type == QStringLiteral("error") || object.contains(QStringLiteral("error"))) {
        return {ClaudeVoiceEventKind::Error, {}, redactedErrorSummary(object)};
    }
    return {};
}

QUrlQuery claudeVoiceStreamQuery(const QStringList &vocabulary)
{
    Q_UNUSED(vocabulary)
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("encoding"), QStringLiteral("linear16"));
    query.addQueryItem(QStringLiteral("sample_rate"), QStringLiteral("16000"));
    query.addQueryItem(QStringLiteral("channels"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("endpointing_ms"), QStringLiteral("300"));
    query.addQueryItem(QStringLiteral("utterance_end_ms"), QStringLiteral("1000"));
    query.addQueryItem(QStringLiteral("language"), QStringLiteral("en"));
    query.addQueryItem(QStringLiteral("use_conversation_engine"), QStringLiteral("true"));
    if (typedInterimsEnabled()) {
        query.addQueryItem(QStringLiteral("forward_interims"), QStringLiteral("typed"));
    }
    query.addQueryItem(QStringLiteral("stt_provider"), QStringLiteral("deepgram-nova3"));
    return query;
}

QByteArray claudeVoiceKeytermsHeader(const QStringList &vocabulary)
{
    QByteArray header;
    QSet<QByteArray> seen;
    for (const QString &value : vocabulary) {
        const QString simplified = value.simplified();
        const QByteArray term = simplified.toLatin1();
        const QByteArray key = term.toLower();
        if (term.isEmpty()
            || QString::fromLatin1(term) != simplified
            || seen.contains(key)) {
            continue;
        }
        const qsizetype separatorBytes = header.isEmpty() ? 0 : 1;
        if (header.size() + separatorBytes + term.size() > kMaximumKeytermBytes) {
            continue;
        }
        seen.insert(key);
        if (!header.isEmpty()) {
            header.append(',');
        }
        header.append(term);
    }
    return header;
}

} // namespace speecher
