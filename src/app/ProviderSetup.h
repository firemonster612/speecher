#pragma once

namespace speecher {

class LocalModelStore;
class ProviderRegistry;
class SecretStore;

// Registers every speech and refinement provider this build offers. Shared by
// the running app and the headless `speecher transcribe`, which builds its own
// registry rather than borrow the app's. Registering creates no provider, so
// a caller that only lists them may pass null for the stores.
void registerProviders(ProviderRegistry &registry, SecretStore *secrets, const LocalModelStore *localModels);

} // namespace speecher
