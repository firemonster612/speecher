#include "dictation/StartupPreparationRunner.h"

#include <QDebug>
#include <QThread>

#include <utility>

namespace speecher {

struct StartupPreparationRunner::Preparation {
    StartupPreparationResult result;
    std::optional<SpeechPrepareJob> speechJob;
    std::optional<RefinementRefreshJob> refinerJob;
};

StartupPreparationRunner::StartupPreparationRunner(QObject *parent)
    : QObject(parent)
{
}

StartupPreparationRunner::~StartupPreparationRunner()
{
    cancel();
    const QList<QThread *> threads = m_threads;
    for (QThread *thread : threads) {
        thread->requestInterruption();
        thread->quit();
        thread->wait();
        delete thread;
    }
}

void StartupPreparationRunner::start(quint64 generation,
                                     std::optional<SpeechPrepareJob> speechJob,
                                     std::optional<RefinementRefreshJob> refinerJob,
                                     SpeechPrepareResult speechPrepared)
{
    cancel();

    auto preparation = std::make_shared<Preparation>();
    preparation->result.generation = generation;
    preparation->result.speech = std::move(speechPrepared);
    preparation->speechJob = std::move(speechJob);
    preparation->refinerJob = std::move(refinerJob);

    QThread *thread = QThread::create([preparation] {
        if (preparation->speechJob) {
            preparation->result.speech = preparation->speechJob->run
                ? preparation->speechJob->run()
                : SpeechPrepareResult{false, QStringLiteral("Speech provider startup job unavailable")};
        }
        if (preparation->result.speech.ok && preparation->refinerJob) {
            preparation->result.refinerRefreshAttempted = true;
            preparation->result.refinerRefresh = preparation->refinerJob->run
                ? preparation->refinerJob->run()
                : RefinementRefreshResult{false, QStringLiteral("Refinement refresh job unavailable")};
        }
    });
    m_current = preparation;
    m_threads.append(thread);

    connect(thread, &QThread::finished, this, [this, thread, preparation] {
        m_threads.removeOne(thread);
        if (m_current != preparation) {
            qInfo() << "startup preparation result ignored";
            return;
        }
        m_current.reset();

        if (preparation->speechJob && preparation->speechJob->apply) {
            preparation->speechJob->apply(preparation->result.speech);
        }
        if (preparation->result.speech.ok
            && preparation->refinerJob
            && preparation->refinerJob->apply) {
            preparation->refinerJob->apply(preparation->result.refinerRefresh);
        }
        emit completed(preparation->result);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void StartupPreparationRunner::cancel()
{
    if (!m_current) {
        return;
    }
    m_current.reset();
    qInfo() << "startup preparation cancelled";
}

} // namespace speecher
