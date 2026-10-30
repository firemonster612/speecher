#include "providers/SignInExpiry.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace speecher::SignInExpiry {

namespace {

QMutex mutex;
QHash<QString, QDateTime> expiries;

} // namespace

void record(const QString &signIn, const QDateTime &expiresAt)
{
    const QMutexLocker lock(&mutex);
    expiries.insert(signIn, expiresAt);
}

bool lapsed(const QString &signIn)
{
    const QMutexLocker lock(&mutex);
    const QDateTime expiresAt = expiries.value(signIn);
    return expiresAt.isValid() && expiresAt <= QDateTime::currentDateTimeUtc();
}

} // namespace speecher::SignInExpiry
