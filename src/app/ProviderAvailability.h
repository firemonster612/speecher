#pragma once

#include "core/settings/SettingsSchema.h"

#include <QHash>
#include <QObject>

namespace speecher {

class NetworkReachability;

// The facts about whether a provider can work right now that nothing slow
// has to be asked for: the operating system's reachability and the sign-ins
// last seen. LocalSetup adds them to the LiveFacts settings and setup read,
// beside the downloaded models and runners it knows itself, and
// fallbackProblem() judges each provider from the lot.
class ProviderAvailability final : public QObject {
    Q_OBJECT

public:
    explicit ProviderAvailability(const NetworkReachability &reachability, QObject *parent = nullptr);

    // Whatever learns a sign-in's state in the course of its own work notes it
    // here: a credential row that read it, a dictation the provider turned
    // away. Nothing reads a keyring or refreshes a sign-in to fill this in, so
    // a provider never noted stays unknown.
    void noteSignIn(const QString &providerId, bool signedIn);
    void addTo(LiveFacts &facts) const;

signals:
    void changed();

private:
    const NetworkReachability &m_reachability;
    QHash<QString, bool> m_signedIn;
};

} // namespace speecher
