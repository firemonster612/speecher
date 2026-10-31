#pragma once

// A fake D-Bus service for suites that run on a private session bus.

#include <QDBusConnection>
#include <QDBusVirtualObject>

// A service on a connection of its own, answering calls to `path` and below.
// A subclass answers them in handleMessage() through m_bus.
class FakeDBusService : public QDBusVirtualObject {
public:
    explicit FakeDBusService(const QString &connectionName)
        : m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName))
    {
    }
    ~FakeDBusService() override { QDBusConnection::disconnectFromBus(m_bus.name()); }

    bool start(const QString &service, const QString &path)
    {
        return m_bus.registerService(service)
            && m_bus.registerVirtualObject(path, this, QDBusConnection::SubPath);
    }

    QString introspect(const QString &) const override { return {}; }

protected:
    QDBusConnection m_bus;
};
