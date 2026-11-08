#pragma once

// The Service Order: the services Speecher tries for one job, speech or
// refinement, first to last, and what each runs with. The first is the main
// service; the rest stand in when it is unavailable. ProviderChain.h turns an
// order into the chain a Dictation Session walks.

#include <QHash>
#include <QString>
#include <QStringList>

namespace speecher {

// A Service Order holds at most this many services.
inline constexpr int kMaxServices = 3;

// What one service runs with in an order, or what a Writing Profile or a run
// asks of the service it puts first. An empty field leaves it to the
// service, or, in a profile's or a run's choice, to the order's entry for
// that service (mergedServiceChoice).
struct ServiceChoice {
    // A provider key ("claude", "openai", "local", "endpoint").
    QString service;
    // The model it runs: a Local Model id, a Local Runner's or a server's
    // model name, an OpenAI or Anthropic model. Claude Voice and ChatGPT
    // dictation have none to choose.
    QString model;
    // Speech, for every entry but the first, which listens for the Spoken
    // Language: the language this one listens for. Empty takes the rule
    // chainSpeechSettings() applies.
    QString language;
    // Refinement on ChatGPT ("none" to "xhigh") and Claude ("low" to "max").
    QString effort;
    // Refinement on ChatGPT ("standard", "fast" or "ultrafast", see
    // effectiveOpenAiSpeed) and Claude ("standard" or "fast").
    QString speed;

    bool operator==(const ServiceChoice &other) const = default;
};

struct ServiceOrder {
    // Provider keys, the main service first. Never more than kMaxServices
    // once stored.
    QStringList order;
    // By provider key. A service keeps its choice while it is out of the
    // order, for when it comes back.
    QHash<QString, ServiceChoice> choices;

    // The first service, or empty for an empty order.
    QString main() const { return order.value(0); }
    // The service's stored choice, or one naming only the service.
    ServiceChoice choice(const QString &service) const;
    // The service's stored choice, added when it has none.
    ServiceChoice &choice(const QString &service);
    // This order with service first and the others after it in their order:
    // how one service runs it once the walk reaches it.
    ServiceOrder withMain(const QString &service) const;

    bool operator==(const ServiceOrder &other) const = default;
};

// over's fields where it has them, base's otherwise: a profile's or a run's
// choice over the order's entry for the same service.
ServiceChoice mergedServiceChoice(const ServiceChoice &base, const ServiceChoice &over);

// What a new install runs: Claude Voice for speech, with Parakeet as the
// Local Model once one is chosen; ChatGPT for refinement, each of ChatGPT and
// Claude with its model, effort and speed.
ServiceOrder defaultSpeechServices();
ServiceOrder defaultRefinementServices();

} // namespace speecher
