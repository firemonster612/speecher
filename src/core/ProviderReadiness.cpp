#include "core/ProviderReadiness.h"

#include "core/EndpointSettings.h"
#include "core/LocalModelCatalog.h"
#include "core/settings/SpokenLanguages.h"

#include <QUrl>

#include <algorithm>

namespace speecher {

namespace {

const QString kLocal = QStringLiteral("local");
const QString kEndpoint = QStringLiteral("endpoint");

// A server on this computer or its network, which keeps answering without
// internet: localhost, a bare or .local name, or a private address.
bool isLocalNetworkServer(const QString &url)
{
    const QString host = QUrl(url).host().toLower();
    if (host.contains(QLatin1Char(':'))) {
        return host == QStringLiteral("::1") || host.startsWith(QStringLiteral("fc"))
            || host.startsWith(QStringLiteral("fd")) || host.startsWith(QStringLiteral("fe80"));
    }
    if (!host.contains(QLatin1Char('.')) || host.endsWith(QStringLiteral(".local"))
        || host.endsWith(QStringLiteral(".lan")) || host.endsWith(QStringLiteral(".home.arpa"))) {
        return true;
    }
    const QStringList parts = host.split(QLatin1Char('.'));
    QList<int> octets;
    for (const QString &part : parts) {
        bool number = false;
        octets.append(part.toInt(&number));
        if (!number) {
            return false;
        }
    }
    if (octets.size() != 4) {
        return false;
    }
    return octets[0] == 127 || octets[0] == 10 || (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31)
        || (octets[0] == 192 && octets[1] == 168) || (octets[0] == 169 && octets[1] == 254);
}

QString serverUrl(ProviderRole role, const AppSettings &settings)
{
    return role == ProviderRole::Speech ? settings.speech.endpoint.baseUrl
                                        : resolvedRefinementEndpoint(settings.refinement).apiBase;
}

// "Ollama isn't running", or that no runner is.
QString runnerNotRunning(const AppSettings &settings)
{
    const QString runner = settings.refinement.localRunner.runner;
    return runner.isEmpty() ? QStringLiteral("No Ollama, LM Studio or llama-server is running")
                            : QStringLiteral("%1 isn't running").arg(localRunnerName(runner));
}

// A Local Model not downloaded, or a Local Runner with no cleanup model named.
QString noModel(ProviderRole role)
{
    return role == ProviderRole::Speech ? QStringLiteral("No model downloaded")
                                        : QStringLiteral("No cleanup model chosen");
}

// The reason a provider's report gives, "Not signed in to ChatGPT.". label is
// the provider's registry label.
QString problemText(FallbackProblem problem, ProviderRole role, const AppSettings &settings,
                    const QString &providerId, const QString &label)
{
    switch (problem) {
    case FallbackProblem::None:
    // spokenLanguageProblem() words this one.
    case FallbackProblem::SpokenLanguage:
        break;
    case FallbackProblem::Offline:
        return QStringLiteral("Can't reach %1 right now.").arg(signInName(providerId, label));
    case FallbackProblem::SignedOut:
        return QStringLiteral("Not signed in to %1.").arg(signInName(providerId, label));
    case FallbackProblem::NoModel:
        return noModel(role) + QLatin1Char('.');
    case FallbackProblem::NoRunner:
        return runnerNotRunning(settings) + QLatin1Char('.');
    case FallbackProblem::NoServer:
        return QStringLiteral("No server URL is set.");
    }
    return {};
}

ProviderReport providerReport(ProviderRole role, const QString &id, const QString &label,
                              const AppSettings &settings, const LiveFacts &facts)
{
    const FallbackProblem problem = fallbackProblem(role, id, settings, facts);
    const bool localRunner = role == ProviderRole::Refinement && id == kLocal;
    ProviderReport report{id, role, label, providerSignsIn(id)};
    report.configured = problem != FallbackProblem::NoModel && problem != FallbackProblem::NoServer
        && !(localRunner && settings.refinement.localRunner.runner.isEmpty());
    if (facts.signedIn.contains(id)) {
        report.signedIn = facts.signedIn.value(id);
    }
    if (problem == FallbackProblem::SpokenLanguage) {
        SpeechSettings speech = settings.speech;
        speech.providerId = id;
        report.problem = spokenLanguageProblem(speech, label);
    } else {
        report.problem = problemText(problem, role, settings, id, label);
    }
    const bool signInUnseen = report.signsIn && !report.signedIn;
    const bool runnerUnchecked = localRunner && (!facts.runnersChecked || facts.detectingRunners);
    if (problem != FallbackProblem::None) {
        report.usable = false;
    } else if (!signInUnseen && !runnerUnchecked) {
        report.usable = true;
    }
    return report;
}

} // namespace

bool fallbackSkipsSpokenLanguage(const SpeechSettings &speech, const QString &providerId)
{
    SpeechSettings candidate = speech;
    candidate.providerId = providerId;
    return !listensForSpokenLanguage(candidate);
}

FallbackProblem fallbackProblem(ProviderRole role, const QString &providerId, const AppSettings &settings,
                                const LiveFacts &facts)
{
    if (providerId == kLocal && role == ProviderRole::Speech) {
        if (!facts.downloadedModels.contains(settings.speech.local.modelId)) {
            return FallbackProblem::NoModel;
        }
        return fallbackSkipsSpokenLanguage(settings.speech, providerId) ? FallbackProblem::SpokenLanguage
                                                                         : FallbackProblem::None;
    }
    if (providerId == kLocal) {
        const QString runner = settings.refinement.localRunner.runner;
        // The Local Runner refuses to clean up without a model.
        if (!runner.isEmpty() && settings.refinement.localRunner.lacksModel()) {
            return FallbackProblem::NoModel;
        }
        const bool running = std::any_of(facts.runners.cbegin(), facts.runners.cend(),
                                         [&runner](const FoundRunner &found) { return found.id == runner; });
        return facts.runnersChecked && !facts.detectingRunners && !running ? FallbackProblem::NoRunner
                                                                            : FallbackProblem::None;
    }
    if (role == ProviderRole::Speech && fallbackSkipsSpokenLanguage(settings.speech, providerId)) {
        return FallbackProblem::SpokenLanguage;
    }
    if (providerId == kEndpoint && serverUrl(role, settings).isEmpty()) {
        return FallbackProblem::NoServer;
    }
    if (!facts.signedIn.value(providerId, true)) {
        return FallbackProblem::SignedOut;
    }
    return facts.reachability == Reachability::Offline && needsInternet(role, providerId, settings)
        ? FallbackProblem::Offline
        : FallbackProblem::None;
}

bool needsInternet(ProviderRole role, const QString &providerId, const AppSettings &settings)
{
    if (providerId == kEndpoint) {
        return !isLocalNetworkServer(serverUrl(role, settings));
    }
    return providerSignsIn(providerId);
}

QList<ProviderReport> providerReports(const AppSettings &settings, const LiveFacts &facts,
                                      const QStringList &speechProviders,
                                      const QStringList &refinementProviders, const ProviderLabels &labels)
{
    QList<ProviderReport> reports;
    for (const QString &id : speechProviders) {
        reports.append(providerReport(ProviderRole::Speech, id, labels(ProviderRole::Speech, id), settings, facts));
    }
    for (const QString &id : refinementProviders) {
        reports.append(
            providerReport(ProviderRole::Refinement, id, labels(ProviderRole::Refinement, id), settings, facts));
    }
    return reports;
}

QList<QStringList> providerReportTable(const QList<ProviderReport> &reports)
{
    const auto state = [](std::optional<bool> known) {
        if (!known) {
            return QStringLiteral("Unknown");
        }
        return *known ? QStringLiteral("Yes") : QStringLiteral("No");
    };
    QList<QStringList> rows{{QStringLiteral("Role"), QStringLiteral("ID"), QStringLiteral("Name"),
                             QStringLiteral("Configured"), QStringLiteral("Signed in"), QStringLiteral("Usable"),
                             QStringLiteral("Problem")}};
    for (const ProviderReport &report : reports) {
        rows.append({report.role == ProviderRole::Speech ? QStringLiteral("Speech") : QStringLiteral("Refinement"),
                     report.id, report.label, state(report.configured),
                     report.signsIn ? state(report.signedIn) : QStringLiteral("-"), state(report.usable),
                     report.problem});
    }
    return rows;
}

bool providerSignsIn(const QString &providerId)
{
    static const QStringList accounts{QStringLiteral("claude"), QStringLiteral("codex"), QStringLiteral("openai"),
                                      QStringLiteral("anthropic")};
    return accounts.contains(providerId);
}

QString signInName(const QString &providerId, const QString &label)
{
    if (providerId == QStringLiteral("codex")) {
        return QStringLiteral("ChatGPT");
    }
    if (providerId == QStringLiteral("claude")) {
        return QStringLiteral("Claude");
    }
    return label;
}

} // namespace speecher
