#pragma once

#include <QObject>
#include <QThread>

#include <functional>
#include <memory>

namespace speecher {

// Runs a provider prepare/refresh job on a throwaway thread and delivers its
// result on context's thread. The job reads provider objects the registry
// owns, so a thread still running when they die reads freed memory (observed
// as heap corruption in the macOS bridge before it joined its probe threads).
// Passing the registry (or the controller that owns it) as owner joins the
// thread first: destroyed() fires before a QObject deletes its children, and
// the connection dies with the thread, so a finished probe costs nothing.
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
    QObject::connect(thread, &QThread::finished, context,
                     [result, done = std::move(done)] { done(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace speecher
