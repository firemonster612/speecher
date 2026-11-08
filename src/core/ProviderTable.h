#pragma once

#include <QString>

#include <optional>

// The built-in speech and refinement providers and the facts the app keys off
// each one. Settings files, the registry, the CLI and the UI name a provider by
// its key, the string a settings file stores; code that branches on a provider
// reads the key into a ProviderId and compares that.

namespace speecher {

enum class ProviderRole {
    Speech,
    Refinement,
};

// Local and Endpoint each serve both roles: Local is This computer (the
// Local Model in speech, the Local Runner in refinement), Endpoint the job's
// server among Your servers.
enum class ProviderId {
    Claude,
    Codex,
    OpenAi,
    Anthropic,
    Local,
    Endpoint,
};

// The refinement provider a run names to refine nothing (`--refine none`).
// Settings say it with RefinementSettings::enabled.
inline const QString kNoRefinementProvider = QStringLiteral("none");

// The provider a key names. Nothing for kNoRefinementProvider, the E2E stub or
// an unknown key.
std::optional<ProviderId> parseProviderId(const QString &key);
QString providerKey(ProviderId id);

bool providerServes(ProviderId id, ProviderRole role);

// The account a provider signs in to, as people know it ("ChatGPT" for both
// OpenAI providers). Empty for one that needs no sign-in.
QString providerAccountName(ProviderId id);

// Whether a person names the model this service runs in this role: every
// refinement service, and the Local Model and a server for speech. Claude
// Voice and ChatGPT dictation have no model to choose.
bool servicePicksModel(ProviderRole role, const QString &providerKey);

} // namespace speecher
