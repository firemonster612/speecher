#pragma once

#include <QDBusObjectPath>
#include <QHash>
#include <QVariantMap>

#include <optional>

namespace speecher {

struct PortalResponse {
    uint status = 0;
    QVariantMap results;
};

class PortalResponseTracker {
public:
    void begin(const QDBusObjectPath &predictedPath)
    {
        m_path = predictedPath;
        m_waitingForHandle = true;
        m_earlyResponses.clear();
    }

    std::optional<PortalResponse> observe(const QString &path,
                                          uint status,
                                          const QVariantMap &results)
    {
        PortalResponse response{status, results};
        if (path == m_path.path()) {
            return response;
        }
        if (m_waitingForHandle) {
            m_earlyResponses.insert(path, response);
        }
        return std::nullopt;
    }

    std::optional<PortalResponse> resolve(const QDBusObjectPath &actualPath)
    {
        m_path = actualPath;
        m_waitingForHandle = false;
        const auto response = m_earlyResponses.constFind(m_path.path());
        if (response == m_earlyResponses.cend()) {
            m_earlyResponses.clear();
            return std::nullopt;
        }
        const PortalResponse result = response.value();
        m_earlyResponses.clear();
        return result;
    }

    QDBusObjectPath path() const { return m_path; }

    void clear()
    {
        m_path = {};
        m_waitingForHandle = false;
        m_earlyResponses.clear();
    }

private:
    QDBusObjectPath m_path;
    bool m_waitingForHandle = false;
    QHash<QString, PortalResponse> m_earlyResponses;
};

} // namespace speecher
