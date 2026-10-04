#include "core/settings/FallbackPresentation.h"

#include "core/EndpointSettings.h"
#include "core/LocalModelCatalog.h"

#include <QUrl>

#include <algorithm>

namespace speecher {
namespace {

const QString kNone = QStringLiteral("none");
const QString kLocal = QStringLiteral("local");
const QString kEndpoint = QStringLiteral("endpoint");

QString primaryOf(const AppSettings &settings, ProviderRole role)
{
    return role == ProviderRole::Speech ? settings.speech.providerId : settings.refinement.providerId;
}

QStringList fallbacksOf(const AppSettings &settings, ProviderRole role)
{
    return normalizedFallbackProviders(role, primaryOf(settings, role),
                                       role == ProviderRole::Speech ? settings.speech.fallbackProviderIds
                                                                    : settings.refinement.fallbackProviderIds);
}

bool offers(const QList<RowOption> &providers, const QString &id)
{
    return std::any_of(providers.cbegin(), providers.cend(),
                       [&id](const RowOption &provider) { return provider.id == id; });
}

// A saved fallback this build doesn't offer, such as a Local Model in a build
// without local speech, stays in the chain until removed and reads as this.
QString labelOf(const QList<RowOption> &providers, const QString &id)
{
    for (const RowOption &provider : providers) {
        if (provider.id == id) {
            return provider.label;
        }
    }
    return QStringLiteral("Provider not in this build");
}

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

QString problemText(FallbackProblem problem, const AppSettings &settings)
{
    switch (problem) {
    case FallbackProblem::None:
        break;
    case FallbackProblem::Offline:
        return QStringLiteral("No internet connection, so it can't stand in right now.");
    case FallbackProblem::SignedOut:
        return QStringLiteral("Not signed in, so it can't stand in yet.");
    case FallbackProblem::NoModel:
        return QStringLiteral("No model downloaded, so it can't stand in yet.");
    case FallbackProblem::NoRunner:
        return runnerNotRunning(settings) + QStringLiteral(", so it can't stand in right now.");
    case FallbackProblem::NoServer:
        return QStringLiteral("No server URL is set, so it can't stand in yet.");
    case FallbackProblem::SpokenLanguage: {
        const LocalModel *model = findLocalModel(settings.speech.local.modelId);
        return QStringLiteral("%1 can't listen for your Spoken Language, so it is skipped.")
            .arg(model ? model->name : settings.speech.local.modelId);
    }
    }
    return {};
}

// Why a fallback can't stand in right now, or empty while it can.
QString unusableReason(ProviderRole role, const QString &id, const AppSettings &settings, const LiveFacts &facts,
                       const QList<RowOption> &providers)
{
    if (!offers(providers, id)) {
        return QStringLiteral("This build of Speecher can't run it, so it can't stand in.");
    }
    return problemText(fallbackProblem(role, id, settings, facts), settings);
}

} // namespace

bool fallbackSkipsSpokenLanguage(const SpeechSettings &speech, const QString &providerId)
{
    const LocalModel *model = providerId == kLocal ? findLocalModel(speech.local.modelId) : nullptr;
    return model && !localModelListensFor(*model, speech.language);
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
        const bool running = std::any_of(facts.runners.cbegin(), facts.runners.cend(),
                                         [&runner](const RowOption &found) { return found.id == runner; });
        return facts.runnersChecked && !facts.detectingRunners && !running ? FallbackProblem::NoRunner
                                                                            : FallbackProblem::None;
    }
    const bool offline = facts.reachability == Reachability::Offline;
    if (providerId == kEndpoint) {
        const QString url = serverUrl(role, settings);
        if (url.isEmpty()) {
            return FallbackProblem::NoServer;
        }
        return offline && !isLocalNetworkServer(url) ? FallbackProblem::Offline : FallbackProblem::None;
    }
    if (!facts.signedIn.value(providerId, true)) {
        return FallbackProblem::SignedOut;
    }
    return offline && providerSignsIn(providerId) ? FallbackProblem::Offline : FallbackProblem::None;
}

QString primaryProviderStatus(ProviderRole role, const AppSettings &settings, const LiveFacts &facts,
                              const QList<RowOption> &providers)
{
    const QString primary = primaryOf(settings, role);
    if (role == ProviderRole::Refinement && primary == kNone) {
        return {};
    }
    const QString account = signInName(primary, labelOf(providers, primary));
    QString problem;
    switch (fallbackProblem(role, primary, settings, facts)) {
    case FallbackProblem::None:
    // The Spoken Language row says so, and dictation stops rather than pass
    // the primary over.
    case FallbackProblem::SpokenLanguage:
        return {};
    case FallbackProblem::Offline:
        problem = QStringLiteral("Can't reach %1 right now.").arg(account);
        break;
    case FallbackProblem::SignedOut:
        problem = QStringLiteral("Not signed in to %1.").arg(account);
        break;
    case FallbackProblem::NoModel:
        problem = QStringLiteral("No model downloaded.");
        break;
    case FallbackProblem::NoRunner:
        problem = runnerNotRunning(settings) + QLatin1Char('.');
        break;
    case FallbackProblem::NoServer:
        problem = QStringLiteral("No server URL is set.");
        break;
    }
    const QStringList fallbacks = fallbacksOf(settings, role);
    const auto usable = std::find_if(fallbacks.cbegin(), fallbacks.cend(), [&](const QString &id) {
        return unusableReason(role, id, settings, facts, providers).isEmpty();
    });
    if (usable != fallbacks.cend()) {
        const QString name = labelOf(providers, *usable);
        return problem + QLatin1Char(' ')
            + (role == ProviderRole::Speech ? QStringLiteral("Dictation starts with %1.").arg(name)
                                            : QStringLiteral("%1 cleans up your words instead.").arg(name));
    }
    return role == ProviderRole::Speech ? problem : problem + QStringLiteral(" Your words are pasted as spoken.");
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

FallbackListPresentation fallbackListPresentation(ProviderRole role, const AppSettings &settings,
                                                  const LiveFacts &facts, const QList<RowOption> &providers,
                                                  FallbackSurface surface)
{
    const QString primary = primaryOf(settings, role);
    if (role == ProviderRole::Refinement && primary == kNone) {
        return {};
    }
    const QStringList fallbacks = fallbacksOf(settings, role);
    FallbackListPresentation list;
    list.heading = QStringLiteral("If %1 is unavailable").arg(labelOf(providers, primary));
    list.subtitle = QStringLiteral("Speecher tries these top to bottom and tells you when it used one.");
    if (role == ProviderRole::Refinement) {
        list.footer = QStringLiteral("If none of these answers, your words are pasted as spoken.");
    }
    for (qsizetype index = 0; index < fallbacks.size(); ++index) {
        const QString &id = fallbacks[index];
        FallbackItem item{id, labelOf(providers, id)};
        item.status = unusableReason(role, id, settings, facts, providers);
        if (!item.status.isEmpty()) {
            item.tone = StatusTone::Negative;
        } else {
            item.status = index == 0
                ? QStringLiteral("Used when %1 is unavailable.").arg(labelOf(providers, primary))
                : QStringLiteral("Used when the ones above are unavailable.");
        }
        item.canMoveUp = index > 0;
        item.canMoveDown = index < fallbacks.size() - 1;
        list.items.append(item);
    }
    for (const RowOption &provider : providers) {
        const bool setupEndpoint =
            surface == FallbackSurface::Setup && role == ProviderRole::Speech && provider.id == kEndpoint;
        if (provider.id != primary && !fallbacks.contains(provider.id) && !setupEndpoint
            && isChainProviderId(role, provider.id)) {
            list.addChoices.append({provider.id, provider.label});
        }
    }
    list.canAdd = fallbacks.size() < kMaxFallbackProviders && !list.addChoices.isEmpty();
    list.addLabel = QStringLiteral("Add a fallback");
    list.addHelp = QStringLiteral("Used when everything above is unavailable.");
    list.addPlaceholder = QStringLiteral("Choose…");
    list.moveUpCaption = QStringLiteral("Move up");
    list.moveDownCaption = QStringLiteral("Move down");
    list.removeCaption = QStringLiteral("Remove");
    return list;
}

FallbackSummary fallbackSummary(ProviderRole role, const AppSettings &settings, const LiveFacts &facts,
                                const QList<RowOption> &providers)
{
    const QString primary = primaryOf(settings, role);
    if (role == ProviderRole::Refinement && primary == kNone) {
        return {};
    }
    const QStringList fallbacks = fallbacksOf(settings, role);
    if (fallbacks.isEmpty()) {
        return {role == ProviderRole::Speech
                    ? QStringLiteral("None. Dictation stops if %1 is unavailable.").arg(labelOf(providers, primary))
                    : QStringLiteral("None. Your words are pasted as spoken if %1 is unavailable.")
                          .arg(labelOf(providers, primary))};
    }
    QStringList labels;
    QString problem;
    for (const QString &id : fallbacks) {
        labels.append(labelOf(providers, id));
        if (problem.isEmpty()) {
            problem = unusableReason(role, id, settings, facts, providers);
        }
    }
    const QString summary = labels.join(QStringLiteral(", then "));
    if (problem.isEmpty()) {
        return {summary};
    }
    return {summary + QStringLiteral(". ") + problem, StatusTone::Negative};
}

QStringList withFallbackMoved(const AppSettings &settings, ProviderRole role, int index, int offset)
{
    QStringList fallbacks = fallbacksOf(settings, role);
    const int target = index + offset;
    if (index >= 0 && index < fallbacks.size() && target >= 0 && target < fallbacks.size()) {
        fallbacks.move(index, target);
    }
    return fallbacks;
}

QStringList withFallbackRemoved(const AppSettings &settings, ProviderRole role, int index)
{
    QStringList fallbacks = fallbacksOf(settings, role);
    if (index >= 0 && index < fallbacks.size()) {
        fallbacks.removeAt(index);
    }
    return fallbacks;
}

QStringList withFallbackAdded(const AppSettings &settings, ProviderRole role, const QString &providerId)
{
    return normalizedFallbackProviders(role, primaryOf(settings, role),
                                       fallbacksOf(settings, role) << providerId);
}

} // namespace speecher
