#pragma once

#include <QNetworkRequest>

namespace speecher {

// Endpoint and runner requests must stay on the server the person selected.
// Model downloads use their own policy because Hugging Face redirects to a CDN.
inline QNetworkRequest endpointRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    return request;
}

} // namespace speecher
