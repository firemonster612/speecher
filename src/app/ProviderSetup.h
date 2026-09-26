#pragma once

namespace speecher {

class ProviderRegistry;
class SecretStore;

// Registers every speech and refinement provider this build offers. Shared by
// the running app and the headless `speecher transcribe`, which builds its own
// registry rather than borrow the app's.
void registerProviders(ProviderRegistry &registry, SecretStore *secrets);

} // namespace speecher
