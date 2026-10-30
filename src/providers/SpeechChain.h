#pragma once

#include "core/ProviderReadiness.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QStringList>

#include <functional>

namespace speecher {

class ProviderRegistry;
class PreparationRunner;

// A session's speech providers, primary first, and the one it is on. It walks
// forward only: preparing finds the first provider from the current one on
// that can take the session's audio, passing over one that can't listen for
// its Spoken Language (the primary is the caller's to check), needs the
// internet while the system says it is offline, is unknown, or doesn't stream
// final text when only that will do. It reads keys the settings have not read
// yet and renews sign-ins off this thread, renewing the refiner's sign-in
// alongside, once per walk.
class SpeechChain : public QObject {
    Q_OBJECT

public:
    // The session's transcriber for providerId, connected to it; null for an
    // id the registry does not offer.
    using Acquire = std::function<SpeechTranscriber *(const QString &providerId)>;

    // How a session wants its walks run; the defaults are a Dictation
    // Session's.
    struct Options {
        // Only providers that stream final text take the audio.
        bool streamedFinalsOnly = false;
        // ready() never comes from inside prepare(): a provider with nothing
        // to prepare off this thread still reports from the worker, and
        // cancel() stops it.
        bool readyLater = false;
    };

    SpeechChain(ProviderRegistry *providers, Acquire acquire, QObject *parent = nullptr);

    // Starts over at primary, with fallbacks after it, and forgets the
    // refiner's renewal. Cancels a preparation still running.
    void begin(const QString &primary, const QStringList &fallbacks);
    const QStringList &providerIds() const;
    // The provider the session is on.
    QString currentId() const;
    // Whether the next provider may make up for a failure of this kind.
    bool fallbackRemains(ProviderFailureKind kind) const;

    void setOptions(const Options &options);
    // What the system says about the internet. Only providers that need it
    // are passed over while offline.
    void setReachability(Reachability reachability);
    // Whether the system says it is offline and the provider needs the
    // internet: trying it could only wait out its timeouts.
    bool offlineFor(ProviderRole role, const QString &providerId, const AppSettings &settings) const;

    // Prepares the current provider, or the first after it that can take the
    // audio, with settings: ends in ready() or exhausted(), at once or later.
    // refiner, when given, renews its sign-in alongside, unless it already
    // has in this walk. Cancels a preparation or renewal still running.
    void prepare(const AppSettings &settings, TranscriptRefiner *refiner = nullptr);
    // Passes the current provider over and prepares from the next.
    void prepareNext(const AppSettings &settings, TranscriptRefiner *refiner = nullptr);
    // Renews the sign-in of the provider the session is on, for its next
    // stream; ends in signInRenewed().
    void renewSignIn(SpeechTranscriber *transcriber, const SpeechSettings &speech, const AccountSettings &accounts);
    // Nothing running reports back.
    void cancel();

signals:
    // A provider the walk passed over, and why.
    void passedOver(const QString &providerId, speecher::ProviderAttemptIssue::Stage stage,
                    const speecher::ProviderFailure &failure);
    // A sign-in renews before the provider can take the audio.
    void renewingSignIn();
    // The keys the walk read, in the settings it was given. The session
    // keeps them; the walk goes on with them.
    void secretsResolved(const speecher::AppSettings &settings);
    // currentId() prepared and takes the audio.
    void ready();
    // No provider is left to take the audio.
    void exhausted();
    void signInRenewed(const std::optional<speecher::ProviderFailure> &failure);

private:
    void walk();
    void finishPreparation(const std::optional<ProviderFailure> &failure);
    void useResolvedSecrets(const std::optional<ProviderFailure> &failure, const AppSettings &settings);

    ProviderRegistry *m_providers;
    Acquire m_acquire;
    PreparationRunner *m_runner;
    QStringList m_providerIds;
    int m_index = 0;
    Options m_options;
    Reachability m_reachability = Reachability::Unknown;
    // What the walk under way prepares with.
    AppSettings m_settings;
    QPointer<TranscriptRefiner> m_refiner;
    // The refiner's sign-in renewed in this walk: once, not for each speech
    // provider tried.
    bool m_refinerRefreshed = false;
    // The preparation running renews a sign-in rather than walks.
    bool m_renewing = false;
};

// How many dropped streams a session reopens on one provider, and when an
// attempt's end is routine: one that streamed for a while was healthy and
// refills the budget.
class SpeechAttemptPolicy {
public:
    // How long an attempt must stream before its end counts as a routine
    // rollover rather than a failure. Tests lower it.
    static int stableAttemptMs();
    static void setStableAttemptMs(int ms);
    // The failure for an attempt the provider ended before it was stable: a
    // refusal rather than a rollover, which treating as a drop stops looping.
    static SpeechFailure endedTooSoon(quint64 attemptId);

    // The whole budget, for a new provider or after a healthy stream.
    void refill();
    void refillIfAttemptWasStable();
    // Spends a reconnect; false once none is left.
    bool takeReconnect();
    void attemptStarted();
    bool attemptWasStable() const;

private:
    QElapsedTimer m_attemptClock;
    int m_reconnectsLeft = 0;
};

} // namespace speecher
