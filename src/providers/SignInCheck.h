#pragma once

#include <QString>

namespace speecher {

class SecretStore;
struct AppSettings;
struct SpeechSettings;

// The services whose sign-ins the Accounts page shows.
enum class SignInService { OpenAi, Anthropic, CliProxy };

enum class SignInState {
    SignedIn,
    // A token that has run out, which its refresh token may renew.
    Expired,
    Missing,
    // The keyring did not answer for a secret the sign-in needs.
    Unknown,
};

// What Speecher finds of one sign-in with the settings as saved. detail is
// core's own line: OpenAI's status, or why the sign-in can't be used or
// checked; it is empty where core has nothing to say beyond the state and
// where the sign-in comes from, which the UI words itself.
struct SignInCheck {
    SignInState state = SignInState::Missing;
    QString detail;
    // The CLI Proxy API server the sign-in goes through, if any.
    QString server;
    // The CLI Proxy API account directory it reads, when no server is set.
    QString directory;
};

// Reads the credentials the sign-in mode points at, the way dictation would,
// without renewing anything. settings is a dictation snapshot: of the secrets
// it left unread, only the one this sign-in needs is read, through secrets.
// Credential files and the keyring can block for seconds: worker threads
// only, with a SecretStore of their own.
SignInCheck checkSignIn(SignInService service, AppSettings settings, SecretStore *secrets);
// Renews an expired sign-in with its refresh token over the network, writing
// the rotated tokens back as dictation does, then checks it. CLI Proxy API
// renews the account each of OpenAI and Anthropic has chosen. Worker threads
// only.
SignInCheck renewSignIn(SignInService service, AppSettings settings, SecretStore *secrets);
// The sign-in dictation uses for the service's speech provider: Codex for
// OpenAI, Claude Voice for Anthropic (the only services it takes). Neither
// takes an API key or the CLI Proxy API server, so it can differ from
// checkSignIn's answer. Reads files only; worker threads only.
SignInCheck checkSpeechSignIn(SignInService service, const SpeechSettings &settings);

} // namespace speecher
