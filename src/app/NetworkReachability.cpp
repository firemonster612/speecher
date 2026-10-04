#include "app/NetworkReachability.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QScopeGuard>

namespace speecher {

NetworkReachability::NetworkReachability(QObject *parent)
    : QObject(parent)
{
}

void NetworkReachability::watchSystem()
{
    QElapsedTimer timer;
    timer.start();
    const auto timing = qScopeGuard([&] {
        qInfo() << "[DEBUG-win-slow] reachability_ns" << timer.nsecsElapsed();
    });
    if (qEnvironmentVariableIsSet("DEBUG_SKIP_REACHABILITY")) return;
    if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        qWarning() << "no network information backend; reachability stays unknown";
        return;
    }
    QNetworkInformation *system = QNetworkInformation::instance();
    connect(system, &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability reachability) { setReachability(fromSystem(reachability)); });
    setReachability(fromSystem(system->reachability()));
}

Reachability NetworkReachability::reachability() const
{
    return m_reachability;
}

void NetworkReachability::setReachability(Reachability reachability)
{
    if (reachability == m_reachability) {
        return;
    }
    m_reachability = reachability;
    emit changed();
}

Reachability NetworkReachability::fromSystem(QNetworkInformation::Reachability reachability)
{
    switch (reachability) {
    case QNetworkInformation::Reachability::Online:
        return Reachability::Online;
    case QNetworkInformation::Reachability::Disconnected:
        return Reachability::Offline;
    case QNetworkInformation::Reachability::Unknown:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        break;
    }
    return Reachability::Unknown;
}

} // namespace speecher
