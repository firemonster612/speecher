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

    QString id() const override { return QStringLiteral("e2e-stub"); }
    QString label() const override { return QStringLiteral("E2E stub"); }
    bool requiresRefresh(const SpeechSettings &) const override { return false; }
    SpeechPrepareResult prepare(const SpeechSettings &) override { return {true, {}}; }

    // A couple of words every 200 ms, so the live preview visibly grows.
    void startAttempt(quint64 attemptId, const SpeechSettings &) override
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

    QString id() const override { return QStringLiteral("e2e-stub"); }
    QString label() const override { return QStringLiteral("E2E stub"); }
    bool requiresRefresh(const RefinementSettings &) const override { return false; }
    void refresh(const RefinementSettings &) override {}
    RefinementPrepareResult prepare(const RefinementSettings &) override { return {true, {}}; }
    bool supportsScreenshotContext(const RefinementSettings &) const override { return true; }

    // A word every 100 ms after a pause long enough for the Refining… shimmer
    // to be seen, then the completed sentence.
    void refine(const QString &,
                const QStringList &,
                const RefinementContext &,
                const RefinementSettings &) override
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
