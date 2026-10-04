#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

enum class ProviderRole {
    Speech,
    Refinement,
};

// A primary may be followed by at most this many fallbacks.
inline constexpr int kMaxFallbackProviders = 2;

// Whether id names a provider of this role that a chain may hold. Ids of
// providers missing from this build (a Local Model without local speech)
// still count, so a build without them keeps the user's configuration; the
// session skips them at runtime.
bool isChainProviderId(ProviderRole role, const QString &id);

// The fallbacks as stored: in order, without blanks, "none", unknown ids, the
// primary or repeats, and at most kMaxFallbackProviders. A refinement primary
// of "none" has none.
QStringList normalizedFallbackProviders(ProviderRole role,
                                        const QString &primary,
                                        const QStringList &fallbacks);

// The primary followed by its normalized fallbacks: the order a Dictation
// Session tries them in. Empty for a refinement primary of "none".
QStringList providerChain(ProviderRole role, const QString &primary, const QStringList &fallbacks);

} // namespace speecher
