#pragma once

#include "core/EndpointSettings.h"

#include <QByteArray>
#include <QStringList>

namespace speecher {

// The answer to "Test connection".
struct EndpointCheck {
    bool ok = false;
    // One line a person can act on.
    QString message;
    // The server's models, for the model picker; empty when it lists none.
    QStringList models;
};

// Blocking, a few seconds at most; run them through runProviderProbe.
EndpointCheck checkSpeechEndpoint(const SpeechEndpointSettings &endpoint, int timeoutMs = 5000);
EndpointCheck checkRefinementEndpoint(const RefinementSettings &settings, int timeoutMs = 5000);

// Model ids from an OpenAI-style {"data":[{"id":..}]} or Ollama-style
// {"models":[{"name":..}]} listing.
QStringList modelIdsFromListing(const QByteArray &body);

} // namespace speecher
