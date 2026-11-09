#include "core/ServiceOrder.h"

namespace speecher {

ServiceChoice ServiceOrder::choice(const QString &service) const
{
    return choices.value(service, ServiceChoice{service});
}

ServiceChoice &ServiceOrder::choice(const QString &service)
{
    auto it = choices.find(service);
    if (it == choices.end()) {
        it = choices.insert(service, ServiceChoice{service});
    }
    return *it;
}

ServiceOrder ServiceOrder::withMain(const QString &service) const
{
    ServiceOrder moved = *this;
    moved.order.removeAll(service);
    moved.order.prepend(service);
    return moved;
}

ServiceChoice mergedServiceChoice(const ServiceChoice &base, const ServiceChoice &over)
{
    const auto pick = [](const QString &mine, const QString &theirs) { return mine.isEmpty() ? theirs : mine; };
    return {pick(over.service, base.service), pick(over.model, base.model), pick(over.language, base.language),
            pick(over.effort, base.effort), pick(over.speed, base.speed)};
}

ServiceOrder defaultSpeechServices()
{
    return {{QStringLiteral("claude")},
            {{QStringLiteral("local"), {QStringLiteral("local"), QStringLiteral("parakeet")}}}};
}

ServiceOrder defaultRefinementServices()
{
    return {{QStringLiteral("openai")},
            {{QStringLiteral("openai"),
              {QStringLiteral("openai"), QStringLiteral("gpt-6-luna"), {}, QStringLiteral("none"), QStringLiteral("fast")}},
             {QStringLiteral("anthropic"),
              {QStringLiteral("anthropic"), QStringLiteral("claude-opus-5-5"), {}, QStringLiteral("low"),
               QStringLiteral("fast")}}}};
}

} // namespace speecher
