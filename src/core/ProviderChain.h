#pragma once

#include "core/ProviderTable.h"
#include "core/ServiceOrder.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace speecher {

struct RefinementSettings;
struct SpeechSettings;

// A provider's registry label, by role and id.
using ProviderLabels = std::function<QString(ProviderRole role, const QString &providerId)>;

// Whether id names a provider of this role that a chain may hold. Ids of
// providers missing from this build (a Local Model without local speech)
// still count, so a build without them keeps the user's configuration; the
// session skips them at runtime.
bool isChainProviderId(ProviderRole role, const QString &id);

// The order as stored: services of this role in order, without blanks,
// unknown ids or repeats. Each entry's choice names
// its service; choices of services out of the order stay.
ServiceOrder normalizedServiceOrder(ProviderRole role, ServiceOrder services);

// The services a Dictation Session tries, in order, without blanks, unknown
// ids or repeats. A Writing Profile's service leads the page's whole order.
QStringList serviceChain(ProviderRole role, const QStringList &order);
QStringList serviceChain(const SpeechSettings &speech);
// Empty while refinement is off (Clean up my words).
QStringList serviceChain(const RefinementSettings &refinement);

} // namespace speecher
