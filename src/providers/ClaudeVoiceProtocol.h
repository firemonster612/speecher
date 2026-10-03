#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

namespace speecher {

// spokenLanguage is a SpeechSettings::language; Automatic leaves the
// language out, which Claude Voice takes as detect.
QUrlQuery claudeVoiceStreamQuery(const QString &spokenLanguage);
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
