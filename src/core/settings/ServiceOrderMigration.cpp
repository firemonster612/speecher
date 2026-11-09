#include "core/settings/ServiceOrderMigration.h"

#include "core/AppSettings.h"
#include "core/EndpointUrl.h"
#include "core/ProviderChain.h"
#include "core/settings/ServiceOrderSettingsCodec.h"
#include "core/settings/SettingsKeys.h"

#include <algorithm>

namespace speecher {
namespace {

using ServiceOrderSettingsCodec::defaultRefinementProvider;

ServiceOrder legacySpeechServices(const QSettings &settings)
{
    const QString claude = providerKey(ProviderId::Claude);
    const QString provider = settings.value(SettingsKeys::SpeechProvider, claude).toString();
    ServiceOrder services = defaultSpeechServices();
    services.order = QStringList{provider.isEmpty() ? claude : provider}
        + settings.value(SettingsKeys::SpeechFallbackProviders).toStringList();
    // Settings saved before fallbacks had their own language have none.
    const QString prefix = SettingsKeys::SpeechFallbackLanguages + QLatin1Char('/');
    for (const QString &key : settings.allKeys()) {
        if (key.startsWith(prefix)) {
            services.choice(key.mid(prefix.size())).language = settings.value(key).toString();
        }
    }
    services.choice(providerKey(ProviderId::Local)).model =
        settings.value(SettingsKeys::LocalModel, localModelId(SpeechSettings{})).toString();
    services.choice(providerKey(ProviderId::Endpoint)).model =
        settings.value(SettingsKeys::SpeechEndpointModel).toString().trimmed();
    return services;
}

// The refinement order an older release saved, and in enabled whether it
// cleaned up words at all. A model or effort missing or unknown reads as the
// default's.
ServiceOrder legacyRefinementServices(const QSettings &settings, bool *enabled)
{
    const QString key = SettingsKeys::RefinementProvider;
    const QString provider = settings.contains(key) ? settings.value(key).toString() : defaultRefinementProvider();
    *enabled = provider != kNoRefinementProvider;
    ServiceOrder services = defaultRefinementServices();
    // None kept no order; Clean up my words turned on again starts from the
    // default.
    services.order = *enabled
        ? QStringList{provider} + settings.value(SettingsKeys::RefinementFallbackProviders).toStringList()
        : QStringList{defaultRefinementProvider()};
    const auto speedOf = [&settings](const QString &fastModeKey) {
        return settings.value(fastModeKey, true).toBool() ? QStringLiteral("fast") : QStringLiteral("standard");
    };
    const QString openAi = providerKey(ProviderId::OpenAi);
    const QString anthropic = providerKey(ProviderId::Anthropic);
    services.choice(openAi) = {openAi, settings.value(SettingsKeys::OpenAiModel).toString(), {},
                               settings.value(SettingsKeys::OpenAiEffort).toString(),
                               settings.value(SettingsKeys::OpenAiSpeed).toString()};
    services.choice(anthropic) = {anthropic, settings.value(SettingsKeys::AnthropicModel).toString(), {},
                                  settings.value(SettingsKeys::AnthropicEffort).toString(),
                                  speedOf(SettingsKeys::AnthropicFastMode)};
    services.choice(providerKey(ProviderId::Local)).model =
        settings.value(SettingsKeys::LocalRunnerModel).toString().trimmed();
    services.choice(providerKey(ProviderId::Endpoint)).model =
        settings.value(SettingsKeys::RefinementEndpointModel).toString().trimmed();
    // ChatGPT's Speed replaced its Fast mode, which a missing or unknown
    // speed still follows.
    ServiceOrder fallbacks = defaultRefinementServices();
    fallbacks.choice(openAi).speed = speedOf(SettingsKeys::OpenAiFastMode);
    return ServiceOrderSettingsCodec::withValidRefinementChoices(services, fallbacks);
}

} // namespace

void migrateServiceOrder(QSettings &settings)
{
    constexpr int currentMigrationVersion = 1;
    if (settings.value(SettingsKeys::ServiceOrderMigrationVersion, 0).toInt() >= currentMigrationVersion) {
        return;
    }
    settings.setValue(SettingsKeys::ServiceOrderMigrationVersion, currentMigrationVersion);
    const auto storedAny = [&settings](std::initializer_list<QString> keys) {
        return std::any_of(keys.begin(), keys.end(), [&settings](const QString &key) { return settings.contains(key); });
    };
    const bool fallbackLanguages = std::ranges::any_of(settings.allKeys(), [](const QString &key) {
        return key.startsWith(SettingsKeys::SpeechFallbackLanguages + QLatin1Char('/'));
    });
    if (!settings.contains(SettingsKeys::SpeechServices)
        && (fallbackLanguages
            || storedAny({SettingsKeys::SpeechProvider, SettingsKeys::SpeechFallbackProviders, SettingsKeys::LocalModel,
                          SettingsKeys::SpeechEndpointModel}))) {
        ServiceOrderSettingsCodec::storeSpeech(settings, legacySpeechServices(settings));
    }
    if (!settings.contains(SettingsKeys::RefinementServices)
        && storedAny({SettingsKeys::RefinementProvider, SettingsKeys::RefinementFallbackProviders,
                      SettingsKeys::OpenAiModel, SettingsKeys::OpenAiEffort, SettingsKeys::OpenAiSpeed,
                      SettingsKeys::OpenAiFastMode, SettingsKeys::AnthropicModel, SettingsKeys::AnthropicEffort,
                      SettingsKeys::AnthropicFastMode, SettingsKeys::LocalRunnerModel,
                      SettingsKeys::RefinementEndpointModel})) {
        bool enabled = true;
        // Saved even when it matches the default: the person's saved order
        // is a choice.
        ServiceOrderSettingsCodec::storeChosenRefinement(settings, legacyRefinementServices(settings, &enabled));
        settings.setValue(SettingsKeys::RefinementEnabled, enabled);
    }
    // The CLI Proxy preset becomes the server at the proxy's address with no
    // key of its own, which sends the proxy's (usesProxyKey). A key kept for
    // a Custom server entered before is let go, so it doesn't go to the
    // proxy; the preset stays for an older release.
    if (settings.value(SettingsKeys::RefinementEndpointPreset).toString() == QStringLiteral("cliproxy")) {
        settings.setValue(SettingsKeys::RefinementEndpointBaseUrl,
                          endpointApiBase(settings.value(SettingsKeys::CliproxyBaseUrl).toString()));
        settings.remove(SettingsKeys::RefinementEndpointApiKey);
        QStringList inKeyring = settings.value(SettingsKeys::SecretsInKeyring).toStringList();
        if (inKeyring.removeAll(QStringLiteral("refinement-endpoint-key")) > 0) {
            settings.setValue(SettingsKeys::SecretsInKeyring, inKeyring);
        }
    }
}

} // namespace speecher
