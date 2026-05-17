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

void TranscriptState::clear()
{
    m_finals.clear();
    m_partial.clear();
    emit changed(text());
}

void TranscriptState::setPartial(const QString &partial)
{
    if (m_partial == partial) {
        return;
    }
    m_partial = partial;
    emit changed(text());
}

void TranscriptState::commitFinal(const QString &finalText)
{
    const QString cleaned = finalText.simplified();
    if (!cleaned.isEmpty() && (m_finals.isEmpty() || m_finals.constLast() != cleaned)) {
        m_finals << cleaned;
    }
    m_partial.clear();
    emit changed(text());
}

} // namespace speecher
