#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <thread>

// libatspi uses this when a tree traversal first encounters a non-root object.
extern "C" AtspiAccessible *_atspi_ref_accessible(const char *, const char *);

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("AT_SPI_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qunsetenv("ATSPI_DISABLE_P2P");
    dbus_threads_init_default();

    DBusError error = DBUS_ERROR_INIT;
    DBusConnection *service = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!service) {
        qCritical("Cannot connect fixture: %s", error.message);
        dbus_error_free(&error);
        return 1;
    }
    const QByteArray name(dbus_bus_get_unique_name(service));
    std::atomic<bool> running{true};
    std::thread server([&] {
        while (running) {
            dbus_connection_read_write(service, 10);
            DBusMessage *request = dbus_connection_pop_message(service);
            if (!request) continue;
            if (dbus_message_get_type(request) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
                DBusMessage *reply = nullptr;
                if (dbus_message_is_method_call(request, ATSPI_DBUS_INTERFACE_APPLICATION,
                                                "GetApplicationBusAddress")) {
                    // An inaccessible peer socket must not crash the shared-bus client.
                    const char *address = "unix:path=/nonexistent/speecher-atspi-peer";
                    reply = dbus_message_new_method_return(request);
                    dbus_message_append_args(reply, DBUS_TYPE_STRING, &address, DBUS_TYPE_INVALID);
                } else if (dbus_message_is_method_call(request, ATSPI_DBUS_INTERFACE_ACCESSIBLE,
                                                       "GetRole")) {
                    dbus_uint32_t role = ATSPI_ROLE_ENTRY;
                    reply = dbus_message_new_method_return(request);
                    dbus_message_append_args(reply, DBUS_TYPE_UINT32, &role, DBUS_TYPE_INVALID);
                } else {
                    reply = dbus_message_new_error(request, DBUS_ERROR_UNKNOWN_METHOD, "Unsupported");
                }
                dbus_connection_send(service, reply, nullptr);
                dbus_message_unref(reply);
                dbus_connection_flush(service);
            }
            dbus_message_unref(request);
        }
    });

    // Exercise Speecher's real client initialization, then the libatspi pending
    // reply that crashed during QtAudioInput's nested event loop on Fedora.
    speecher::atspi::TargetSnapshot::capture();
    const bool p2pDisabled = qgetenv("ATSPI_DISABLE_P2P") == "1";
    AtspiAccessible *child = _atspi_ref_accessible(
        name.constData(), "/org/a11y/atspi/accessible/child");
    GError *roleError = nullptr;
    const AtspiRole role = atspi_accessible_get_role(child, &roleError);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 500) {
        g_main_context_iteration(nullptr, false);
        QThread::msleep(1);
    }
    const bool targetRoleRead = !roleError && role == ATSPI_ROLE_ENTRY;
    if (roleError) g_error_free(roleError);
    g_object_unref(child);
    running = false;
    server.join();
    dbus_connection_close(service);
    dbus_connection_unref(service);
    if (!p2pDisabled) {
        qCritical("Did not set ATSPI_DISABLE_P2P before accessibility access");
    }
    if (!targetRoleRead) {
        qCritical("Could not read the target role over the accessibility bus");
    }
    if (!p2pDisabled || !targetRoleRead) {
        return 1;
    }
    qInfo("Survived unavailable peer; target role read over accessibility bus");
    return 0;
}
