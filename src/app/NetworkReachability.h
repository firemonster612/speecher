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

    // Only Disconnected is Offline. Local and Site are Unknown: Windows and
    // NetworkManager report them whenever their connectivity probe is blocked,
    // as behind many corporate proxies, while the internet works.
    static Reachability fromSystem(QNetworkInformation::Reachability reachability);

signals:
    void changed();

private:
    Reachability m_reachability = Reachability::Unknown;
};

} // namespace speecher
