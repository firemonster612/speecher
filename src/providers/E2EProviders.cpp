#include "providers/E2EProviders.h"

#include <QTimer>

namespace speecher {
namespace {

const QString kRawSentence = QStringLiteral(
    "the quick brown fox jumps over the lazy dog and keeps going");
const QString kRefinedSentence = QStringLiteral(
    "The quick brown fox jumps over the lazy dog and keeps going.");

class E2ESpeechTranscriber final : public SpeechTranscriber {
public:
    using SpeechTranscriber::SpeechTranscriber;

    bool requiresRefresh(const SpeechSettings &, const AccountSettings &) const override { return false; }
    std::optional<ProviderFailure> prepare(const SpeechSettings &, const AccountSettings &) override
    {
        return std::nullopt;
    }

    // A couple of words every 200 ms, so the live preview visibly grows.
    void startAttempt(quint64 attemptId, const SpeechSettings &, const SpeechAttemptOptions &) override
    {
        m_attemptId = attemptId;
        m_finalSent = false;
        const QStringList words = kRawSentence.split(QLatin1Char(' '));
        for (int i = 2; i <= words.size(); i += 2) {
            const QString partial = QStringList(words.mid(0, i)).join(QLatin1Char(' '));
            QTimer::singleShot(100 * i, this, [this, attemptId, partial] {
                if (attemptId == m_attemptId && !m_finalSent) {
                    emit partialTranscript(attemptId, partial);
                }
            });
        }
    }

    void sendAudio(quint64, const QByteArray &) override {}

    // Finalisation takes long enough for the Transcribing… shimmer to be seen.
    void finishInput(quint64 attemptId) override
    {
        if (attemptId != m_attemptId) {
            return;
        }
        QTimer::singleShot(900, this, [this, attemptId] {
            if (attemptId != m_attemptId || m_finalSent) {
                return;
            }
            m_finalSent = true;
            emit finalTranscript(attemptId, kRawSentence);
            emit attemptCompleted(attemptId);
        });
    }

    void cancelAttempt(quint64 attemptId) override
    {
        if (attemptId == m_attemptId) {
            m_attemptId = 0;
        }
    }

private:
    quint64 m_attemptId = 0;
    bool m_finalSent = false;
};

class E2ETranscriptRefiner final : public TranscriptRefiner {
public:
    using TranscriptRefiner::TranscriptRefiner;

    std::optional<ProviderFailure> prepare(const RefinementSettings &, const AccountSettings &) override
    {
        return std::nullopt;
    }

    // A word every 100 ms after a pause long enough for the Refining… shimmer
    // to be seen, then the completed sentence.
    void refine(const QString &,
                const QStringList &,
                const RefinementContext &,
                const RefinementSettings &,
                const AccountSettings &) override
    {
        const quint64 generation = ++m_generation;
        const QStringList words = kRefinedSentence.split(QLatin1Char(' '));
        for (int i = 0; i < words.size(); ++i) {
            const QString chunk = (i ? QStringLiteral(" ") : QString()) + words.at(i);
            QTimer::singleShot(700 + 100 * i, this, [this, generation, chunk] {
                if (generation == m_generation) {
                    emit delta(chunk);
                }
            });
        }
        QTimer::singleShot(800 + 100 * words.size(), this, [this, generation] {
            if (generation == m_generation) {
                emit completed(kRefinedSentence);
            }
        });
    }

    void cancel() override { ++m_generation; }

private:
    quint64 m_generation = 0;
};

} // namespace

SpeechTranscriber *createE2ESpeechTranscriber(QObject *parent)
{
    return new E2ESpeechTranscriber(parent);
}

TranscriptRefiner *createE2ETranscriptRefiner(QObject *parent)
{
    return new E2ETranscriptRefiner(parent);
}

} // namespace speecher
