#include "providers/CustomEndpoints.h"
#include "providers/EndpointRequest.h"

#include "core/EndpointUrl.h"
#include "providers/BlockingHttp.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace speecher {
namespace {

EndpointCheck checkFromListing(const HttpResult &result, const QString &url)
{
    if (result.status == 0 || (result.status < 400 && !result.error.isEmpty())) {
        return {false, QStringLiteral("Could not reach %1: %2").arg(url, result.error), {}};
    }
    if (result.status == 401 || result.status == 403) {
        return {false, QStringLiteral("The server refused the API key (HTTP %1).").arg(result.status), {}};
    }
    const QStringList models = result.status == 200 ? modelIdsFromListing(result.body) : QStringList{};
    if (models.isEmpty()) {
        return {true, QStringLiteral("Connected. The server does not list its models; type one."), {}};
    }
    return {true, QStringLiteral("Connected. Models available: %1.").arg(models.size()), models};
}

} // namespace

QStringList modelIdsFromListing(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    QStringList ids;
    for (const QJsonValue &entry : object.value(QStringLiteral("data")).toArray()) {
        ids << entry.toObject().value(QStringLiteral("id")).toString();
    }
    for (const QJsonValue &entry : object.value(QStringLiteral("models")).toArray()) {
        ids << entry.toObject().value(QStringLiteral("name")).toString();
    }
    ids.removeAll(QString());
    return ids;
}

EndpointCheck checkSpeechEndpoint(const SpeechEndpointSettings &endpoint, int timeoutMs)
{
    if (endpoint.baseUrl.isEmpty()) {
        return {false, QStringLiteral("Enter the server URL."), {}};
    }
    const QString url = endpoint.baseUrl + QStringLiteral("/v1/models");
    QNetworkRequest request = endpointRequest(QUrl(url));
    if (!endpoint.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + endpoint.apiKey.toUtf8());
    }
    return checkFromListing(blockingGet(request, timeoutMs), endpoint.baseUrl);
}

EndpointCheck checkRefinementEndpoint(const RefinementSettings &settings, int timeoutMs)
{
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings);
    if (endpoint.apiBase.isEmpty()) {
        return {false, QStringLiteral("Enter the server URL."), {}};
    }
    QNetworkRequest request = endpointRequest(QUrl(endpoint.apiBase + QStringLiteral("/models")));
    if (endpoint.format == QStringLiteral("anthropic")) {
        request.setRawHeader("anthropic-version", "2023-06-01");
        if (!endpoint.apiKey.isEmpty()) request.setRawHeader("x-api-key", endpoint.apiKey.toUtf8());
    } else if (!endpoint.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + endpoint.apiKey.toUtf8());
    }
    const HttpResult listing = blockingGet(request, timeoutMs);
    EndpointCheck check = checkFromListing(listing, endpoint.apiBase);
    if (!check.ok || !check.models.isEmpty() || endpoint.format == QStringLiteral("anthropic")) {
        return check;
    }
    // Ollama's own listing names every pulled model, including ones its
    // OpenAI-compatible listing leaves out.
    const HttpResult tags = blockingGet(
        endpointRequest(QUrl(endpointServerBase(endpoint.apiBase) + QStringLiteral("/api/tags"))),
        timeoutMs);
    const QStringList models = tags.status == 200 ? modelIdsFromListing(tags.body) : QStringList{};
    return models.isEmpty() ? check : checkFromListing(tags, endpoint.apiBase);
}

} // namespace speecher
