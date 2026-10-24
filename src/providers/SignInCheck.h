#pragma once

#include <QString>

namespace speecher {

class SecretStore;
struct RefinementSettings;

// The services whose sign-ins the Accounts page shows.
enum class SignInService { OpenAi, Anthropic, CliProxy };

enum class SignInState {
    SignedIn,
    // A token that has run out, which its refresh token may renew.
    Expired,
    Missing,
};

// What Speecher finds of one sign-in with the settings as saved: its state,
// and core's status line on where it came from or why there is none.
struct SignInCheck {
    SignInState state = SignInState::Missing;
    QString detail;
};

// Reads the credentials the sign-in mode points at, the way dictation would,
// without renewing anything. secrets reads the saved OpenAI key. Credential
// files and the keyring can block for seconds: worker threads only, with a
// SecretStore of their own.
SignInCheck checkSignIn(SignInService service, const RefinementSettings &settings, SecretStore *secrets);
// Renews an expired sign-in with its refresh token over the network, writing
// the rotated tokens back as dictation does, then checks it. CLI Proxy API
// renews each of its expired account files. Worker threads only.
SignInCheck renewSignIn(SignInService service, const RefinementSettings &settings, SecretStore *secrets);

} // namespace speecher
