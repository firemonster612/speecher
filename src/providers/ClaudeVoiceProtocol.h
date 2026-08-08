#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

namespace speecher {

QUrlQuery claudeVoiceStreamQuery(const QStringList &vocabulary);
QByteArray claudeVoiceKeytermsHeader(const QStringList &vocabulary);

enum class ClaudeVoiceEventKind {
    Unknown,
    Working,
    Endpoint,
    TranscriptError,
    Error,
};

struct ClaudeVoiceEvent {
    ClaudeVoiceEventKind kind = ClaudeVoiceEventKind::Unknown;
    QString data;
    QString errorSummary;
};

ClaudeVoiceEvent parseClaudeVoiceEvent(const QString &message);

} // namespace speecher
