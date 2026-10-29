#pragma once

#include <QString>

#include <optional>

// The built-in speech and refinement providers and the facts the app keys off
// each one. Settings files, the registry, the CLI and the UI name a provider by
// its key, the string a settings file stores; code that branches on a provider
// reads the key into a ProviderId and switches on that.

namespace speecher {

struct RefinementSettings;
struct SpeechSettings;

enum class ProviderRole {
    Speech,
    Refinement,
};

// Local and Endpoint each serve both roles: in speech they are the Local
// Model and the speech Custom Endpoint, in refinement the Local Runner and the
// refinement Custom Endpoint.
enum class ProviderId {
    Claude,
    Codex,
    OpenAi,
    Anthropic,
    Local,
    Endpoint,
};

// The refinement provider key that settings hold while nothing refines.
inline const QString kNoRefinementProvider = QStringLiteral("none");

// The provider a key names. Nothing for kNoRefinementProvider, the E2E stub or
// an unknown key.
std::optional<ProviderId> parseProviderId(const QString &key);
QString providerKey(ProviderId id);

bool providerServes(ProviderId id, ProviderRole role);

// The account a provider signs in to, as people know it ("ChatGPT" for both
// OpenAI providers). Empty for one that needs no sign-in.
QString providerAccountName(ProviderId id);

// The setting holding the model a provider runs. Null for a key outside the
// role and for Claude and ChatGPT dictation, whose model is not a setting.
QString *speechModelSetting(SpeechSettings &speech, const QString &providerKey);
const QString *speechModelSetting(const SpeechSettings &speech, const QString &providerKey);
QString *refinementModelSetting(RefinementSettings &refinement, const QString &providerKey);
const QString *refinementModelSetting(const RefinementSettings &refinement, const QString &providerKey);

} // namespace speecher
