#include "core/TranscriptState.h"

namespace speecher {

TranscriptState::TranscriptState(QObject *parent)
    : QObject(parent)
{
}

QString TranscriptState::text() const
{
    QStringList parts = m_finals;
    if (!m_partial.trimmed().isEmpty()) {
        parts << m_partial.trimmed();
    }
    return parts.join(' ').simplified();
}

QString TranscriptState::partial() const
{
    return m_partial;
}

bool TranscriptState::isEmpty() const
{
    return text().trimmed().isEmpty();
}

QString TranscriptState::heardLanguage() const
{
    return m_heardLanguage;
}

void TranscriptState::clear()
{
    m_finals.clear();
    m_partial.clear();
    m_attemptBase.clear();
    m_attemptLanguage.clear();
    m_heardLanguage.clear();
    emit changed(text());
}

void TranscriptState::setPartial(const QString &partial)
{
    if (m_partial == partial) {
        return;
    }
    m_partial = partial;
    if (!partial.trimmed().isEmpty()) {
        m_heardLanguage = m_attemptLanguage;
    }
    emit changed(text());
}

void TranscriptState::commitFinal(const QString &finalText)
{
    const QString cleaned = finalText.simplified();
    if (!cleaned.isEmpty()) {
        m_finals << cleaned;
        m_heardLanguage = m_attemptLanguage;
    }
    m_partial.clear();
    emit changed(text());
}

void TranscriptState::replaceFinals(const QString &text)
{
    const QString cleaned = text.simplified();
    if (cleaned.isEmpty()) {
        return;
    }
    m_finals = {cleaned};
    m_partial.clear();
    emit changed(this->text());
}

void TranscriptState::startAttempt(const QString &spokenLanguage)
{
    m_attemptBase = text();
    m_attemptLanguage = spokenLanguage;
}

void TranscriptState::replaceAttemptFinals(const QString &attemptText)
{
    if (!attemptText.trimmed().isEmpty()) {
        m_heardLanguage = m_attemptLanguage;
    }
    replaceFinals(m_attemptBase.isEmpty() ? attemptText : m_attemptBase + QLatin1Char(' ') + attemptText);
}

} // namespace speecher
