#include "core/settings/ServiceOrderSettingsCodec.h"

#include "core/AppSettings.h"
#include "core/CliToolDiscovery.h"
#include "core/ProviderChain.h"
#include "core/settings/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher::ServiceOrderSettingsCodec {
namespace {

bool isOpenAiEffort(const QString &effort)
{
    return effort == QStringLiteral("none") || effort == QStringLiteral("low") || effort == QStringLiteral("medium")
        || effort == QStringLiteral("high") || effort == QStringLiteral("xhigh");
}

bool isAnthropicEffort(const QString &effort)
{
    return effort == QStringLiteral("low") || effort == QStringLiteral("medium") || effort == QStringLiteral("high")
        || effort == QStringLiteral("xhigh") || effort == QStringLiteral("max");
}

bool isOpenAiSpeed(const QString &speed)
{
    return speed == QStringLiteral("standard") || speed == QStringLiteral("fast")
        || speed == QStringLiteral("ultrafast");
}

QByteArray toJson(const ServiceOrder &services)
{
    QJsonObject choices;
    for (auto it = services.choices.cbegin(); it != services.choices.cend(); ++it) {
        QJsonObject choice;
        const auto put = [&choice](const char *name, const QString &value) {
            if (!value.isEmpty()) choice.insert(QLatin1String(name), value);
        };
        put("model", it->model.trimmed());
        put("language", it->language);
        put("effort", it->effort);
        put("speed", it->speed);
        choices.insert(it.key(), choice);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("order"), QJsonArray::fromStringList(services.order)},
                                     {QStringLiteral("choices"), choices}})
        .toJson(QJsonDocument::Compact);
}

// The order stored under key over defaults.
ServiceOrder fromJson(const QSettings &settings, const QString &key, ServiceOrder defaults)
{
    const QJsonObject object = QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
    if (!object.value(QStringLiteral("order")).isArray()) {
        return defaults;
    }
    defaults.order.clear();
    for (const QJsonValue &id : object.value(QStringLiteral("order")).toArray()) {
        defaults.order.append(id.toString());
    }
    const QJsonObject choices = object.value(QStringLiteral("choices")).toObject();
    for (auto it = choices.constBegin(); it != choices.constEnd(); ++it) {
        const QJsonObject choice = it.value().toObject();
        const auto field = [&choice](const char *name) { return choice.value(QLatin1String(name)).toString(); };
        defaults.choices.insert(it.key(), {it.key(), field("model"), field("language"), field("effort"), field("speed")});
    }
    return defaults;
}

} // namespace

ServiceOrder speech(const QSettings &settings)
{
    return normalizedServiceOrder(ProviderRole::Speech,
                                  fromJson(settings, SettingsKeys::SpeechServices, defaultSpeechServices()));
}

void storeSpeech(QSettings &settings, const ServiceOrder &value)
{
    ServiceOrder services = normalizedServiceOrder(ProviderRole::Speech, value);
    if (localModelId(SpeechSettings{services}) != localModelId(SpeechSettings{speech(settings)})) {
        settings.setValue(SettingsKeys::LocalModelChosen, true);
    }
    if (services.order.isEmpty()) {
        services.order = defaultSpeechServices().order;
    }
    settings.setValue(SettingsKeys::SpeechServices, toJson(services));
}

ServiceOrder refinement(const QSettings &settings)
{
    ServiceOrder defaults = defaultRefinementServices();
    if (!settings.contains(SettingsKeys::RefinementServices)) {
        defaults.order = {defaultRefinementProvider()};
    }
    return withValidRefinementChoices(normalizedServiceOrder(
        ProviderRole::Refinement, fromJson(settings, SettingsKeys::RefinementServices, defaults)));
}

void storeRefinement(QSettings &settings, const ServiceOrder &value)
{
    const ServiceOrder services = withValidRefinementChoices(normalizedServiceOrder(ProviderRole::Refinement, value));
    if (!settings.contains(SettingsKeys::RefinementServices) && services == refinement(settings)) {
        return;
    }
    storeChosenRefinement(settings, services);
}

void storeChosenRefinement(QSettings &settings, const ServiceOrder &value)
{
    ServiceOrder services = withValidRefinementChoices(normalizedServiceOrder(ProviderRole::Refinement, value));
    if (services.order.isEmpty()) {
        services.order = {defaultRefinementProvider()};
    }
    settings.setValue(SettingsKeys::RefinementServices, toJson(services));
}

QString defaultRefinementProvider()
{
    if (CliToolDiscovery::isCodexInstalled()) {
        return providerKey(ProviderId::OpenAi);
    }
    if (CliToolDiscovery::isClaudeCodeInstalled()) {
        return providerKey(ProviderId::Anthropic);
    }
    return providerKey(ProviderId::OpenAi);
}

ServiceOrder withValidRefinementChoices(ServiceOrder services, const ServiceOrder &fallbacks)
{
    for (const ProviderId id : {ProviderId::OpenAi, ProviderId::Anthropic}) {
        const bool openAi = id == ProviderId::OpenAi;
        const ServiceChoice fallback = fallbacks.choice(providerKey(id));
        ServiceChoice &choice = services.choice(providerKey(id));
        choice.model = choice.model.trimmed();
        if (choice.model.isEmpty()) {
            choice.model = fallback.model;
        } else if (!openAi) {
            choice.model = replacedAnthropicModel(choice.model);
        }
        if (!(openAi ? isOpenAiEffort(choice.effort) : isAnthropicEffort(choice.effort))) {
            choice.effort = fallback.effort;
        }
        const bool knownSpeed = openAi ? isOpenAiSpeed(choice.speed)
                                       : choice.speed == QStringLiteral("standard") || choice.speed == QStringLiteral("fast");
        if (!knownSpeed) {
            choice.speed = fallback.speed;
        }
    }
    return services;
}

QString replacedAnthropicModel(const QString &model)
{
    return model == QStringLiteral("claude-haiku-4-5") || model == QStringLiteral("claude-haiku-4-5-20251001")
        ? QStringLiteral("claude-haiku-5-5")
        : model;
}

} // namespace speecher::ServiceOrderSettingsCodec
