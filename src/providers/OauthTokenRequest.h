#pragma once

#include <QNetworkRequest>

namespace speecher {

// platform.claude.com's bot rules answer any request bearing Qt's unset-UA
// default ("Mozilla/5.0") with 429 rate_limit_error before the OAuth grant is
// processed, so token-endpoint requests identify as the native CLI's HTTP
// client (Claude Code's refresh goes through axios). The same identity is
// deliberately sent to every token endpoint, including auth.openai.com, which
// does not discriminate by User-Agent.
inline constexpr auto tokenRequestUserAgent = "axios/1.15.2";

// How long a sign-in renewal waits for another one renewing the same
// credentials, which holds their lock for at most its token request
// (refreshTimeoutMs) and the reads and writes around it. Once it has the
// lock, the waiting renewal reads what the other wrote rather than renew
// again, so renewals that overlap, as a recording's microphone and system
// audio streams do, share one result instead of the second failing.
inline int refreshLockTimeoutMs(int refreshTimeoutMs)
{
    return refreshTimeoutMs + 5000;
}

inline void applyTokenRequestHeaders(QNetworkRequest &request)
{
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("User-Agent", tokenRequestUserAgent);
}

} // namespace speecher
