#pragma once

#include "core/settings/SettingsSchema.h"

#include <QNetworkInformation>
#include <QObject>

namespace speecher {

// Whether this computer can reach the internet, as the operating system
// reports it through Qt's networkinformation plugin. It never probes a server.
// ApplicationController owns the one instance settings and dictation share.
class NetworkReachability final : public QObject {
    Q_OBJECT

public:
    explicit NetworkReachability(QObject *parent = nullptr);

    // Starts following the operating system's reports. Where no backend
    // loads, as when the plugin is missing, the answer stays Unknown.
    void watchSystem();
    Reachability reachability() const;
    // What each report sets; a test sets it directly.
    void setReachability(Reachability reachability);

    // Unknown stays Unknown. Anything short of Online (no network, only this
    // computer, only the local network) is Offline.
    static Reachability fromSystem(QNetworkInformation::Reachability reachability);

signals:
    void changed();

private:
    Reachability m_reachability = Reachability::Unknown;
};

} // namespace speecher
