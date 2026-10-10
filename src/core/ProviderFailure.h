#pragma once

#include <QMetaType>
#include <QString>

namespace speecher {

// Why a speech or refinement provider could not do its job, as far as the
// provider can tell. The kind decides whether a Dictation Session moves on to
// the next provider in its chain; the message is the provider's own reason.
enum class ProviderFailureKind {
    // Not classified. Never moves on.
    Other,
    // Missing configuration, sign-in, model, runner or build support.
    Unavailable,
    // The service rejected the sign-in (401, 403, invalid_grant).
    Authentication,
    // Offline, refused, DNS, TLS: the request never got an answer.
    Network,
    Timeout,
    // 5xx, or an error event the server streamed.
    Server,
    // 429.
    RateLimited,
    // The provider answered with something unusable: an empty or truncated
    // result, or one that breaks binding restoration.
    InvalidResult,
    Cancelled,
};

struct ProviderFailure {
    ProviderFailureKind kind = ProviderFailureKind::Other;
    QString message;
    // The HTTP status when there was one, for diagnostics; 0 otherwise.
    int httpStatus = 0;
};

// Whether a failure of this kind sends a Dictation Session on to the next
// provider in its chain.
inline bool permitsProviderFallback(ProviderFailureKind kind)
{
    switch (kind) {
    case ProviderFailureKind::Unavailable:
    case ProviderFailureKind::Authentication:
    case ProviderFailureKind::Network:
    case ProviderFailureKind::Timeout:
    case ProviderFailureKind::Server:
    case ProviderFailureKind::RateLimited:
        return true;
    case ProviderFailureKind::Other:
    case ProviderFailureKind::InvalidResult:
    case ProviderFailureKind::Cancelled:
        return false;
    }
    return false;
}

// Whether a failure of this kind may pass by itself, so the same request
// can go through later.
inline bool isTransientFailure(ProviderFailureKind kind)
{
    return kind == ProviderFailureKind::Network || kind == ProviderFailureKind::Timeout
        || kind == ProviderFailureKind::Server || kind == ProviderFailureKind::RateLimited;
}

} // namespace speecher

Q_DECLARE_METATYPE(speecher::ProviderFailure)
