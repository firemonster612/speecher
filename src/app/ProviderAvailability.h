#pragma once

#include "core/ProviderReadiness.h"

#include <QHash>
#include <QObject>
#include <QSet>

namespace speecher {

class NetworkReachability;
class ProviderRegistry;
struct SpeechSettings;

// The facts about whether a provider can work right now that nothing slow
// has to be asked for: the operating system's reachability and the sign-ins
// last seen. LocalSetup adds them to its LiveFacts, beside the downloaded
// models and runners it knows itself, and fallbackProblem() judges each
// provider from the lot. Only setup asks for the speech sign-ins
// (checkSpeechSignIns); everything else is noted in passing.
class ProviderAvailability final : public QObject {
    Q_OBJECT

public:
    explicit ProviderAvailability(const NetworkReachability &reachability, QObject *parent = nullptr);

    // Whatever learns a sign-in's state in the course of its own work notes it
    // here: a credential row that read it, a check or renewal on Accounts, a
    // dictation the provider turned away. Beyond setup's check below, nothing
    // reads a keyring or refreshes a sign-in just to fill this in, so a
    // provider never noted stays unknown.
    void noteSignIn(const QString &providerId, bool signedIn);
    // Reads each speech service's sign-in the way a dictation does before it
    // starts, which may renew one that has run out, off the GUI thread, and
    // notes what it finds. Setup's Transcription step asks, since nothing else
    // learns a sign-in before the first dictation. A check that can't reach
    // the service says nothing about the sign-in and notes nothing; it
    // leaves its failure instead. A provider already being checked is left
    // to that check.
    void checkSpeechSignIns(ProviderRegistry &registry, const SpeechSettings &speech);
    // Whether that check runs for a provider, and why its last one couldn't
    // reach the service: the provider's message, empty once one gets through
    // and while one runs.
    bool checkingSignIn(const QString &providerId) const { return m_checkingSignIns.contains(providerId); }
    QString signInCheckFailure(const QString &providerId) const { return m_signInCheckFailures.value(providerId); }
    void addTo(LiveFacts &facts) const;

signals:
    void changed();

private:
    const NetworkReachability &m_reachability;
    QHash<QString, bool> m_signedIn;
    QSet<QString> m_checkingSignIns;
    QHash<QString, QString> m_signInCheckFailures;
};

} // namespace speecher
