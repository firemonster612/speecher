#include "core/SettingsStore.h"
#include "providers/PreparationRunner.h"

#include <QDebug>
#include <QThread>

#include <utility>

namespace speecher {

namespace {

std::optional<ProviderFailure> runJob(const PreparationJob &job)
{
    if (!job.run) {
        return ProviderFailure{ProviderFailureKind::Unavailable, QStringLiteral("Provider preparation unavailable")};
    }
    return job.run();
}

void applyJob(const std::optional<PreparationJob> &job, const std::optional<ProviderFailure> &failure)
{
    if (job && job->apply) {
        job->apply(failure);
    }
}

} // namespace

struct PreparationRunner::Preparation {
    // On the worker, unless cancelled first.
    std::function<void(const std::atomic_bool &cancelled)> work;
    // Back on this thread, for the latest preparation only.
    std::function<void()> finish;
    std::atomic_bool cancelled = false;
};

PreparationRunner::PreparationRunner(QObject *parent)
    : QObject(parent)
{
}

PreparationRunner::~PreparationRunner()
{
    cancel();
    const QList<QThread *> threads = m_threads;
    for (QThread *thread : threads) {
        thread->requestInterruption();
        thread->quit();
        if (thread->wait(100)) {
            delete thread;
        }
    }
}

void PreparationRunner::prepareSpeech(std::optional<PreparationJob> speechJob,
                                      std::optional<PreparationJob> refinerJob)
{
    struct Speech {
        std::optional<PreparationJob> speechJob;
        std::optional<PreparationJob> refinerJob;
        std::optional<ProviderFailure> speechFailure;
        std::optional<ProviderFailure> refinerFailure;
    };
    auto speech = std::make_shared<Speech>();
    speech->speechJob = std::move(speechJob);
    speech->refinerJob = std::move(refinerJob);
    run(
        [speech](const std::atomic_bool &cancelled) {
            if (speech->speechJob) {
                speech->speechFailure = runJob(*speech->speechJob);
            }
            if (!cancelled && !speech->speechFailure && speech->refinerJob) {
                speech->refinerFailure = runJob(*speech->refinerJob);
            }
        },
        [this, speech] {
            applyJob(speech->speechJob, speech->speechFailure);
            if (!speech->speechFailure) {
                applyJob(speech->refinerJob, speech->refinerFailure);
            }
            emit speechPrepared(speech->speechFailure);
        });
}

void PreparationRunner::prepareRefiner(PreparationJob refinerJob)
{
    struct Refiner {
        std::optional<PreparationJob> job;
        std::optional<ProviderFailure> failure;
    };
    auto refiner = std::make_shared<Refiner>();
    refiner->job = std::move(refinerJob);
    run([refiner](const std::atomic_bool &) { refiner->failure = runJob(*refiner->job); },
        [this, refiner] {
            applyJob(refiner->job, refiner->failure);
            emit refinerPrepared(refiner->failure);
        });
}

void PreparationRunner::resolveSecrets(ProviderRole role, const QString &providerId, AppSettings settings)
{
    struct Secrets {
        AppSettings settings;
        std::optional<ProviderFailure> failure;
    };
    auto secrets = std::make_shared<Secrets>();
    secrets->settings = std::move(settings);
    run(
        [secrets, role, providerId](const std::atomic_bool &) {
            const QString error = SettingsStore::resolveProviderSecrets(secrets->settings, role, providerId);
            if (!error.isEmpty()) {
                secrets->failure = ProviderFailure{ProviderFailureKind::Unavailable, error};
            }
        },
        [this, secrets] { emit secretsResolved(secrets->failure, secrets->settings); });
}

void PreparationRunner::run(std::function<void(const std::atomic_bool &)> work, std::function<void()> finish)
{
    cancel();

    auto preparation = std::make_shared<Preparation>();
    preparation->work = std::move(work);
    preparation->finish = std::move(finish);
    QThread *thread = QThread::create([preparation] {
        if (!preparation->cancelled) {
            preparation->work(preparation->cancelled);
        }
    });
    m_current = preparation;
    m_threads.append(thread);

    connect(thread, &QThread::finished, this, [this, thread, preparation] {
        m_threads.removeOne(thread);
        if (m_current != preparation) {
            qInfo() << "provider preparation result ignored";
            return;
        }
        m_current.reset();
        preparation->finish();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void PreparationRunner::cancel()
{
    if (!m_current) {
        return;
    }
    m_current->cancelled = true;
    m_current.reset();
    qInfo() << "provider preparation cancelled";
}

} // namespace speecher
