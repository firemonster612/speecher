#include "app/NetworkReachability.h"

#include <QDebug>

namespace speecher {

NetworkReachability::NetworkReachability(QObject *parent)
    : QObject(parent)
{
}

void NetworkReachability::watchSystem()
{
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
    case QNetworkInformation::Reachability::Unknown:
        return Reachability::Unknown;
    case QNetworkInformation::Reachability::Online:
        return Reachability::Online;
    case QNetworkInformation::Reachability::Disconnected:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        break;
    }
    return Reachability::Offline;
}

} // namespace speecher
