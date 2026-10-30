#include "dictation/EditReview.h"

#include <utility>

namespace speecher {
namespace {

QStringList merged(QStringList earlier, const QStringList &later)
{
    for (const QString &id : later) {
        if (!earlier.contains(id)) {
            earlier.append(id);
        }
    }
    return earlier;
}

} // namespace

bool EditReview::isUp() const
{
    return m_revision.has_value();
}

bool EditReview::followingUp() const
{
    return m_followingUp;
}

const EditReview::Revision &EditReview::revision() const
{
    return *m_revision;
}

void EditReview::revise(Revision revision)
{
    if (m_followingUp && m_revision) {
        const Revision &before = *m_revision;
        revision.instructions = before.instructions + revision.instructions;
        revision.spokenWords += before.spokenWords;
        revision.listeningMs += before.listeningMs;
        // Insights names every provider the edit's instructions ran on,
        // earliest first.
        revision.providerHistory.speechRan =
            merged(before.providerHistory.speechRan, revision.providerHistory.speechRan);
        revision.providerHistory.refinementRan =
            merged(before.providerHistory.refinementRan, revision.providerHistory.refinementRan);
    }
    m_followingUp = false;
    m_revision = std::move(revision);
}

void EditReview::startFollowUp()
{
    m_followingUp = true;
}

void EditReview::endFollowUp()
{
    m_followingUp = false;
}

void EditReview::end()
{
    m_followingUp = false;
    m_revision.reset();
}

bool EditReview::setKeys(const ReviewKeys &keys)
{
    return std::exchange(m_keys, keys) != keys;
}

const ReviewKeys &EditReview::keys() const
{
    return m_keys;
}

SelectionEditReview EditReview::presentation() const
{
    SelectionEditReview review =
        selectionEditReview(m_revision->target.selectedText, m_revision->edit, m_revision->instructions, m_keys);
    review.following = m_followingUp;
    return review;
}

} // namespace speecher
