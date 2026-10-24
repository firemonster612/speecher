#pragma once

// Whether each provider can work right now, judged only from what the app
// already knows: nothing here refreshes a sign-in, asks a keyring or probes a
// server. A Dictation Session decides which fallbacks to pass over with it,
// and `speecher providers` reports it.

#include "core/AppSettings.h"
#include "core/ProviderChain.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

// Whether this computer can reach the internet, as the operating system says.
// Unknown, where it cannot say, never counts as offline.
enum class Reachability { Unknown, Offline, Online };

// A Local Runner found on this computer.
struct FoundRunner {
    // As LocalRunnerSettings stores it.
    QString id;
    // Its name with the version, "Ollama 0.12.3".
    QString label;
};

// A graphics card as one transcribe.cpp backend reports it.
struct LocalGpu {
    // A localBackends kind other than "auto" or "cpu".
    QString backend;
    // The PCI bus id, empty when the backend reports none.
    QString deviceId;
    QString description;
};

// What the app layer last learned about this computer and the servers a
// person named. LocalSetup builds it, and ProviderAvailability adds
// reachability and the sign-ins last seen. fallbackProblem() and
// `speecher providers` judge readiness from part of it; the rest, such as the
// model folder, the graphics cards and the endpoints' and runners' model
// lists, is for the UI's Local models and Speech pages to show.
struct LiveFacts {
    // The last connection check's verdict, "Checking…" while one runs, empty
    // before the first. LocalSetup runs one on its own once an endpoint in
    // use is filled in, as well as when asked.
    QString speechEndpointStatus;
    QStringList speechEndpointModels;
    // Whether a check runs, and whether the last one reached the server.
    bool speechEndpointChecking = false;
    bool speechEndpointOk = false;
    QString refinementEndpointStatus;
    QStringList refinementEndpointModels;
    bool refinementEndpointChecking = false;
    bool refinementEndpointOk = false;
    QList<FoundRunner> runners;
    // What each found runner can serve, by runner id.
    QHash<QString, QStringList> runnerModels;
    bool detectingRunners = false;
    // A look for runners has finished, so an empty runners means none runs.
    bool runnersChecked = false;
    Reachability reachability = Reachability::Unknown;
    // Whether a provider's sign-in was last seen present, by provider id. A
    // provider missing here is unknown: nothing asks a keyring or refreshes a
    // sign-in to fill this in.
    QHash<QString, bool> signedIn;
    // Where Local Model files live and how much room they take.
    QString modelFolder;
    // Local Model ids with a finished download, in catalog order.
    QStringList downloadedModels;
    // The graphics cards a Local Model can run on, once per backend that
    // reaches them.
    QList<LocalGpu> localGpus;
    // Where the loaded Local Model runs, e.g. "NVIDIA GeForce RTX 3060
    // (CUDA)"; empty while none is loaded.
    QString localModelRunsOn;
};

// Why a provider can't stand in right now.
enum class FallbackProblem {
    None,
    Offline,
    SignedOut,
    NoModel,
    NoRunner,
    NoServer,
    // A speech fallback that can't listen for the Spoken Language is skipped.
    SpokenLanguage,
};

FallbackProblem fallbackProblem(ProviderRole role, const QString &providerId, const AppSettings &settings,
                                const LiveFacts &facts);

// One provider as `speecher providers` reports it, judged by fallbackProblem().
struct ProviderReport {
    QString id;
    ProviderRole role = ProviderRole::Speech;
    // The registry label.
    QString label;
    // Whether it takes a sign-in: Claude, ChatGPT, OpenAI and Anthropic do.
    bool signsIn = false;
    // Holds what it needs from settings: a downloaded model, a server URL, a
    // chosen Local Runner and its cleanup model.
    bool configured = false;
    // Empty for a provider that doesn't sign in, and for one whose sign-in
    // nothing has seen; signsIn tells them apart.
    std::optional<bool> signedIn;
    // Empty while the verdict turns on what nobody has checked: a sign-in not
    // seen, or whether the Local Runner is running.
    std::optional<bool> usable;
    // "Not signed in to ChatGPT."; empty while nothing known stops it.
    QString problem;
};

// A report for each provider id, speech first, in their order. labels names
// each one as the registry does.
QList<ProviderReport> providerReports(const AppSettings &settings, const LiveFacts &facts,
                                      const QStringList &speechProviders,
                                      const QStringList &refinementProviders, const ProviderLabels &labels);

// `speecher providers`' table: a row of column captions, then a row of cells
// per report. A state nobody has checked reads "Unknown", a sign-in a
// provider doesn't take "-".
QList<QStringList> providerReportTable(const QList<ProviderReport> &reports);

// Whether a speech fallback is skipped because it can't listen for the
// Spoken Language, by its own language list or its Local Model's catalog
// entry (listensForSpokenLanguage). A missing model is not skipped here; it
// is unavailable. The primary is never skipped.
bool fallbackSkipsSpokenLanguage(const SpeechSettings &speech, const QString &providerId);

// Whether a provider needs the internet to work: one that signs in, or a
// Custom Endpoint whose server isn't on this computer or its network.
bool needsInternet(ProviderRole role, const QString &providerId, const AppSettings &settings);

// Whether a provider works through an account the person signs in to (Claude
// Voice, ChatGPT Codex, OpenAI, Anthropic), rather than a key or this computer.
bool providerSignsIn(const QString &providerId);
// The account a provider signs in to, as a person knows it: "ChatGPT" for
// ChatGPT Codex. label is the provider's registry label.
QString signInName(const QString &providerId, const QString &label);

} // namespace speecher
