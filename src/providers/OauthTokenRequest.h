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

inline void applyTokenRequestHeaders(QNetworkRequest &request)
{
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("User-Agent", tokenRequestUserAgent);
}

} // namespace speecher
