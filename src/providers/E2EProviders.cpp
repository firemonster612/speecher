#include "providers/E2EProviders.h"

#include "dictation/DictationPorts.h"

#include <QTimer>

namespace speecher {
namespace {

class E2ESpeechTranscriber final : public SpeechTranscriber {
public:
    using SpeechTranscriber::SpeechTranscriber;

    QString id() const override { return QStringLiteral("e2e-stub"); }
    QString label() const override { return QStringLiteral("E2E stub"); }
    bool requiresRefresh(const SpeechSettings &) const override { return false; }
    SpeechPrepareResult prepare(const SpeechSettings &) override { return {true, {}}; }

    void startAttempt(quint64 attemptId, const SpeechSettings &) override
    {
        m_attemptId = attemptId;
        m_finalSent = false;
        QTimer::singleShot(1000, this, [this, attemptId] {
            if (attemptId != m_attemptId || m_finalSent) {
                return;
            }
            m_finalSent = true;
            emit finalTranscript(attemptId, QStringLiteral("the quick brown fox"));
        });
    }

    void sendAudio(quint64, const QByteArray &) override {}

    void finishInput(quint64 attemptId) override
    {
        if (attemptId != m_attemptId) {
            return;
        }
        if (!m_finalSent) {
            m_finalSent = true;
            emit finalTranscript(attemptId, QStringLiteral("the quick brown fox"));
        }
        emit attemptCompleted(attemptId);
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

    void refine(const QString &,
                const QStringList &,
                const RefinementContext &,
                const RefinementSettings &) override
    {
        const quint64 generation = ++m_generation;
        QTimer::singleShot(300, this, [this, generation] {
            if (generation == m_generation) {
                emit completed(QStringLiteral("The quick brown fox."));
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
