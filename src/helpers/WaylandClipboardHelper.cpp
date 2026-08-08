#include "ext-data-control-v1-client-protocol.h"

#include <QByteArray>
#include <QHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <wayland-client.h>

namespace {

struct ClipboardOwner {
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    ext_data_control_manager_v1 *manager = nullptr;
    ext_data_control_device_v1 *device = nullptr;
    ext_data_control_source_v1 *source = nullptr;
    QHash<QByteArray, QByteArray> parts;
    bool running = true;
};

void writeAll(int fd, const QByteArray &data)
{
    qsizetype offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(fd, data.constData() + offset,
                                        static_cast<size_t>(data.size() - offset));
        if (written > 0) {
            offset += written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(fd);
}

void sourceSend(void *data, ext_data_control_source_v1 *, const char *mimeType, int fd)
{
    const auto *owner = static_cast<ClipboardOwner *>(data);
    writeAll(fd, owner->parts.value(QByteArray(mimeType)));
}

void sourceCancelled(void *data, ext_data_control_source_v1 *)
{
    static_cast<ClipboardOwner *>(data)->running = false;
}

constexpr ext_data_control_source_v1_listener sourceListener{
    .send = sourceSend,
    .cancelled = sourceCancelled,
};

void offerMime(void *, ext_data_control_offer_v1 *, const char *)
{
}

constexpr ext_data_control_offer_v1_listener offerListener{
    .offer = offerMime,
};

void deviceDataOffer(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer)
{
    ext_data_control_offer_v1_add_listener(offer, &offerListener, nullptr);
}

void deviceSelection(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer)
{
    if (offer) {
        ext_data_control_offer_v1_destroy(offer);
    }
}

void deviceFinished(void *data, ext_data_control_device_v1 *)
{
    static_cast<ClipboardOwner *>(data)->running = false;
}

void devicePrimarySelection(void *, ext_data_control_device_v1 *,
                            ext_data_control_offer_v1 *offer)
{
    if (offer) {
        ext_data_control_offer_v1_destroy(offer);
    }
}

constexpr ext_data_control_device_v1_listener deviceListener{
    .data_offer = deviceDataOffer,
    .selection = deviceSelection,
    .finished = deviceFinished,
    .primary_selection = devicePrimarySelection,
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version)
{
    auto *owner = static_cast<ClipboardOwner *>(data);
    if (std::strcmp(interface, wl_seat_interface.name) == 0 && !owner->seat) {
        owner->seat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface, qMin(version, 9U)));
    } else if (std::strcmp(interface, ext_data_control_manager_v1_interface.name) == 0
               && !owner->manager) {
        owner->manager = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t)
{
}

constexpr wl_registry_listener registryListener{
    .global = registryGlobal,
    .global_remove = registryGlobalRemove,
};

bool readPayload(ClipboardOwner *owner)
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        std::fputs("Could not read clipboard payload\n", stderr);
        return false;
    }
    const QByteArray bytes = input.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        std::fputs("Invalid clipboard payload\n", stderr);
        return false;
    }

    const QJsonArray parts = document.object().value(QStringLiteral("parts")).toArray();
    for (const QJsonValue &value : parts) {
        const QJsonObject part = value.toObject();
        const QByteArray mimeType = part.value(QStringLiteral("mime")).toString().toUtf8();
        if (mimeType.isEmpty() || mimeType.contains('\0')) {
            std::fputs("Invalid clipboard MIME type\n", stderr);
            return false;
        }
        owner->parts.insert(
            mimeType,
            QByteArray::fromBase64(part.value(QStringLiteral("data")).toString().toLatin1()));
    }
    return true;
}

void cleanUp(ClipboardOwner *owner)
{
    if (owner->source) {
        ext_data_control_source_v1_destroy(owner->source);
    }
    if (owner->device) {
        ext_data_control_device_v1_destroy(owner->device);
    }
    if (owner->manager) {
        ext_data_control_manager_v1_destroy(owner->manager);
    }
    if (owner->seat) {
        wl_seat_destroy(owner->seat);
    }
    if (owner->registry) {
        wl_registry_destroy(owner->registry);
    }
    if (owner->display) {
        wl_display_disconnect(owner->display);
    }
}

} // namespace

int main()
{
    ClipboardOwner owner;
    if (!readPayload(&owner)) {
        return EXIT_FAILURE;
    }

    owner.display = wl_display_connect(nullptr);
    if (!owner.display) {
        std::fputs("Could not connect to the Wayland compositor\n", stderr);
        return EXIT_FAILURE;
    }
    owner.registry = wl_display_get_registry(owner.display);
    wl_registry_add_listener(owner.registry, &registryListener, &owner);
    if (wl_display_roundtrip(owner.display) < 0 || !owner.seat || !owner.manager) {
        std::fputs("Wayland data-control is unavailable\n", stderr);
        cleanUp(&owner);
        return EXIT_FAILURE;
    }

    owner.device = ext_data_control_manager_v1_get_data_device(owner.manager, owner.seat);
    ext_data_control_device_v1_add_listener(owner.device, &deviceListener, &owner);

    if (owner.parts.isEmpty()) {
        ext_data_control_device_v1_set_selection(owner.device, nullptr);
    } else {
        owner.source = ext_data_control_manager_v1_create_data_source(owner.manager);
        ext_data_control_source_v1_add_listener(owner.source, &sourceListener, &owner);
        for (auto it = owner.parts.cbegin(); it != owner.parts.cend(); ++it) {
            ext_data_control_source_v1_offer(owner.source, it.key().constData());
        }
        ext_data_control_device_v1_set_selection(owner.device, owner.source);
    }

    if (wl_display_roundtrip(owner.display) < 0) {
        std::fputs("Could not publish the clipboard selection\n", stderr);
        cleanUp(&owner);
        return EXIT_FAILURE;
    }
    std::fputs("READY\n", stdout);
    std::fflush(stdout);

    if (!owner.parts.isEmpty()) {
        while (owner.running && wl_display_dispatch(owner.display) >= 0) {
        }
    }
    cleanUp(&owner);
    return EXIT_SUCCESS;
}
