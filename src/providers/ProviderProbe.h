#pragma once

#include <QObject>
#include <QPointer>
#include <QThread>

#include <functional>
#include <memory>

namespace speecher {

// Runs a provider prepare/refresh job on a throwaway thread and delivers its
// result on context's thread. The job and its completion read provider
// objects the registry owns, so pass the registry (or the controller that
// owns it) as owner; it protects both halves:
// - The destroyed→wait() join covers the worker thread: destroyed() fires
//   before a QObject deletes its children, so `work` never reads freed
//   provider memory (observed as heap corruption in the macOS bridge before
//   it joined its probe threads). The join is unbounded on purpose, matching
//   the mac bridge: quitting with a probe in flight can wait as long as the
//   job's own network timeout.
// - The QPointer covers the queued completion: `done` usually captures a
//   provider too, and a context that outlives the owner would otherwise run
//   it on a freed one.
// Whether a late result still matters is the caller's business.
template <typename Result>
void runProviderProbe(QObject *owner,
                      QObject *context,
                      std::function<Result()> work,
                      std::function<void(const Result &)> done)
{
    auto result = std::make_shared<Result>();
    QThread *thread = QThread::create([work = std::move(work), result] { *result = work(); });
    QObject::connect(owner, &QObject::destroyed, thread,
                     [thread] { thread->wait(); }, Qt::DirectConnection);
    QPointer<QObject> ownerAlive(owner);
    QObject::connect(thread, &QThread::finished, context,
                     [result, done = std::move(done), ownerAlive] {
                         if (!ownerAlive) {
                             return;
                         }
                         done(*result);
                     });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace speecher
