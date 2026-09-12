// speecher-keywatchd: reads the keyboards and reports whether one key is
// down. It never runs as root: the systemd unit starts it as the dedicated
// speecher-keywatch system user with the input group, an empty capability
// bounding set and a syscall allowlist — the sandbox is the unit's, not
// hand-rolled in here. A client names one allowlisted key; every other key's
// events are read and dropped inside this process. See
// keywatch-security-design.md.
//
// Plain C++, no Qt, no D-Bus. libsystemd only, for the activated socket.

#include "KeywatchProtocol.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-daemon.h>

namespace {

using speecher::keywatch::KeyEvent;
using speecher::keywatch::permittedKeyById;
using speecher::keywatch::PermittedKey;
using speecher::keywatch::Refusal;
using speecher::keywatch::WatchReply;
using speecher::keywatch::WatchRequest;

constexpr char inputDirectory[] = "/dev/input";
constexpr int maxClients = 16;
constexpr int idleExitSeconds = 30;
// Per-uid connection budget: no more than this many WATCH attempts in the
// window, so the allowlist cannot be rebuilt out of repeated connects.
constexpr int rateLimitConnects = 8;
constexpr int rateLimitWindowSec = 60;

void logLine(const std::string &text)
{
    const std::string line = "speecher-keywatchd: " + text + "\n";
    (void)!write(STDERR_FILENO, line.data(), line.size());
}

std::uint64_t monotonicUsec()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return std::uint64_t(now.tv_sec) * 1000000 + std::uint64_t(now.tv_nsec) / 1000;
}

struct Keyboard {
    std::string name; // The directory entry, such as "event3".
    int fd = -1;
};

struct Client {
    int fd = -1;
    uid_t uid = 0;
    pid_t pid = 0;
    std::uint16_t evdev = 0; // 0 until a WATCH is accepted.
    bool down = false;
    // A WATCH may arrive fragmented; parse only a whole request.
    std::uint8_t pending[sizeof(WatchRequest)] = {};
    std::size_t pendingFill = 0;
};

struct RateBucket {
    uid_t uid = 0;
    int count = 0;
    std::uint64_t windowStartUsec = 0;
};

// Opens one /dev/input node if it is a real keyboard: EV_KEY with the letter
// range (a mouse advertises EV_KEY without letters), and not a virtual
// device — ydotoold's uinput keyboard is how Speecher's own text delivery
// would otherwise loop back into the watch. Returns -1 for everything else.
int openKeyboard(const std::string &name)
{
    if (name.compare(0, 5, "event") != 0) {
        return -1;
    }
    const std::string path = std::string(inputDirectory) + "/" + name;
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    unsigned long evBits = 0;
    unsigned char keyBits[(KEY_MAX / 8) + 1] = {};
    input_id id{};
    const bool keyboard = ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), &evBits) >= 0
        && (evBits & (1UL << EV_KEY))
        && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0
        && (keyBits[KEY_A / 8] & (1 << (KEY_A % 8)))
        && (keyBits[KEY_Z / 8] & (1 << (KEY_Z % 8)))
        && ioctl(fd, EVIOCGID, &id) >= 0
        && id.bustype != BUS_VIRTUAL;
    if (!keyboard) {
        close(fd);
        return -1;
    }
    return fd;
}

class Server {
public:
    explicit Server(int listenFd)
        : m_listenFd(listenFd)
    {
    }

    // Startup enumeration. False when devices exist but none could be opened,
    // which means the service user lacks the input group: exiting loudly is
    // better than running deaf.
    bool openKeyboards()
    {
        DIR *dir = opendir(inputDirectory);
        if (!dir) {
            logLine(std::string("could not open ") + inputDirectory);
            return false;
        }
        int refused = 0;
        while (dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            const int fd = openKeyboard(name);
            if (fd >= 0) {
                m_keyboards.push_back({name, fd});
            } else if (errno == EACCES) {
                refused += 1;
            }
        }
        closedir(dir);
        if (m_keyboards.empty() && refused > 0) {
            logLine("input devices exist but none could be opened; is the "
                    "service running with the input group?");
            return false;
        }
        return true;
    }

    int run()
    {
        m_epoll = epoll_create1(EPOLL_CLOEXEC);
        if (m_epoll < 0) {
            return 1;
        }
        m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_inotify >= 0) {
            // IN_ATTRIB as well: a new node exists before udev grants the
            // input group, so the open that matters follows the chmod.
            inotify_add_watch(m_inotify, inputDirectory, IN_CREATE | IN_ATTRIB);
            addToEpoll(m_inotify);
        }
        addToEpoll(m_listenFd);
        for (const Keyboard &keyboard : m_keyboards) {
            addToEpoll(keyboard.fd);
        }
        std::uint64_t idleDeadline = monotonicUsec() + idleExitSeconds * 1000000ULL;
        std::array<epoll_event, 32> events{};
        while (true) {
            int timeoutMs = -1;
            if (m_clientCount == 0) {
                const std::uint64_t now = monotonicUsec();
                if (now >= idleDeadline) {
                    return 0; // Idle: exit so no key reader lingers unused.
                }
                timeoutMs = int((idleDeadline - now) / 1000) + 1;
            }
            const int ready = epoll_wait(m_epoll, events.data(), events.size(), timeoutMs);
            if (ready < 0 && errno != EINTR) {
                return 1;
            }
            const bool hadClients = m_clientCount > 0;
            for (int index = 0; index < ready; ++index) {
                dispatch(events[index].data.fd);
            }
            if (hadClients && m_clientCount == 0) {
                idleDeadline = monotonicUsec() + idleExitSeconds * 1000000ULL;
            }
        }
    }

private:
    void addToEpoll(int fd) const
    {
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = fd;
        epoll_ctl(m_epoll, EPOLL_CTL_ADD, fd, &event);
    }

    void dispatch(int fd)
    {
        if (fd == m_listenFd) {
            acceptClient();
            return;
        }
        if (fd == m_inotify) {
            readInotify();
            return;
        }
        for (Keyboard &keyboard : m_keyboards) {
            if (fd == keyboard.fd) {
                readKeyboard(keyboard);
                return;
            }
        }
        for (Client &client : m_clients) {
            if (client.fd == fd) {
                readClient(client);
                return;
            }
        }
    }

    bool alreadyOpen(const std::string &name) const
    {
        for (const Keyboard &keyboard : m_keyboards) {
            if (keyboard.name == name) {
                return true;
            }
        }
        return false;
    }

    // Hot-plug: open a keyboard that appeared after startup.
    void readInotify()
    {
        alignas(inotify_event) char buffer[4096];
        ssize_t got = 0;
        while ((got = read(m_inotify, buffer, sizeof(buffer))) > 0) {
            for (ssize_t offset = 0; offset < got;) {
                const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
                offset += ssize_t(sizeof(inotify_event)) + event->len;
                if (event->len == 0) {
                    continue;
                }
                const std::string name = event->name;
                if (alreadyOpen(name)) {
                    continue;
                }
                const int fd = openKeyboard(name);
                if (fd >= 0) {
                    m_keyboards.push_back({name, fd});
                    addToEpoll(fd);
                    logLine("watching new keyboard " + name);
                }
            }
        }
    }

    void dropKeyboard(Keyboard &keyboard)
    {
        logLine("keyboard " + keyboard.name + " went away");
        epoll_ctl(m_epoll, EPOLL_CTL_DEL, keyboard.fd, nullptr);
        close(keyboard.fd);
        for (auto it = m_keyboards.begin(); it != m_keyboards.end(); ++it) {
            if (it->fd == keyboard.fd) {
                m_keyboards.erase(it);
                return;
            }
        }
    }

    bool rateLimited(uid_t uid)
    {
        const std::uint64_t now = monotonicUsec();
        RateBucket *existing = nullptr;
        RateBucket *free = nullptr;
        for (RateBucket &bucket : m_rate) {
            if (bucket.count > 0
                && now - bucket.windowStartUsec >= rateLimitWindowSec * 1000000ULL) {
                bucket = RateBucket{}; // The window closed; the bucket is free again.
            }
            if (bucket.count > 0 && bucket.uid == uid) {
                existing = &bucket;
            } else if (bucket.count == 0 && !free) {
                free = &bucket;
            }
        }
        if (existing) {
            if (existing->count >= rateLimitConnects) {
                return true;
            }
            existing->count += 1;
            return false;
        }
        if (free) {
            free->uid = uid;
            free->windowStartUsec = now;
            free->count = 1;
        }
        return false; // No free bucket: fail open rather than lock everyone out.
    }

    void acceptClient()
    {
        const int fd = accept4(m_listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return;
        }
        ucred credentials{};
        socklen_t length = sizeof(credentials);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
            close(fd);
            return;
        }
        if (m_clientCount >= maxClients || rateLimited(credentials.uid)) {
            const WatchReply reply{speecher::keywatch::protocolVersion,
                                   std::uint8_t(Refusal::TooManyRequests)};
            (void)!write(fd, &reply, sizeof(reply));
            logLine("refused uid " + std::to_string(credentials.uid) + ": rate limited");
            close(fd);
            return;
        }
        for (Client &client : m_clients) {
            if (client.fd == -1) {
                client = Client{};
                client.fd = fd;
                client.uid = credentials.uid;
                client.pid = credentials.pid;
                addToEpoll(fd);
                m_clientCount += 1;
                return;
            }
        }
        close(fd);
    }

    void dropClient(Client &client)
    {
        epoll_ctl(m_epoll, EPOLL_CTL_DEL, client.fd, nullptr);
        close(client.fd);
        client = Client{};
        m_clientCount -= 1;
    }

    bool uidAlreadyWatching(const Client &asking) const
    {
        for (const Client &client : m_clients) {
            if (client.fd != -1 && &client != &asking && client.uid == asking.uid
                && client.evdev != 0) {
                return true;
            }
        }
        return false;
    }

    void readClient(Client &client)
    {
        const ssize_t got = read(client.fd,
                                 client.pending + client.pendingFill,
                                 sizeof(client.pending) - client.pendingFill);
        if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) {
            dropClient(client);
            return;
        }
        if (got < 0) {
            return;
        }
        client.pendingFill += std::size_t(got);
        if (client.pendingFill < sizeof(WatchRequest)) {
            return;
        }
        WatchRequest request{};
        std::memcpy(&request, client.pending, sizeof(request));
        client.pendingFill = 0;
        Refusal refusal = Refusal::None;
        const PermittedKey *key = nullptr;
        if (client.evdev != 0 || uidAlreadyWatching(client)) {
            // One watch per peer: neither a second WATCH on this connection
            // nor a second connection from the same uid gets another key.
            refusal = Refusal::AlreadyWatching;
        } else if (request.version != speecher::keywatch::protocolVersion) {
            refusal = Refusal::BadVersion;
        } else if (!(key = permittedKeyById(request.keyId))) {
            refusal = Refusal::KeyNotPermitted;
        }
        const WatchReply reply{speecher::keywatch::protocolVersion, std::uint8_t(refusal)};
        (void)!write(client.fd, &reply, sizeof(reply));
        if (refusal != Refusal::None) {
            logLine("refused uid " + std::to_string(client.uid) + " pid "
                    + std::to_string(client.pid) + " key " + std::to_string(request.keyId));
            if (client.evdev == 0) {
                dropClient(client);
            }
            return;
        }
        client.evdev = key->evdev;
        logLine("watching for uid " + std::to_string(client.uid) + " pid "
                + std::to_string(client.pid) + " key " + std::string(key->code));
    }

    void readKeyboard(Keyboard &keyboard)
    {
        input_event event{};
        ssize_t got = 0;
        while ((got = read(keyboard.fd, &event, sizeof(event))) == sizeof(event)) {
            if (event.type != EV_KEY || event.value == 2) {
                continue; // value 2 is auto-repeat, which is not a transition.
            }
            report(event.code, event.value == 1);
        }
        // An unplugged keyboard reports EOF or ENODEV forever; leaving its fd
        // in the epoll set would spin the loop.
        if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR)) {
            dropKeyboard(keyboard);
        }
    }

    // The only thing that ever leaves the daemon: down/up for a key a client
    // already asked for, with a monotonic timestamp and no key identity.
    void report(std::uint16_t evdev, bool down)
    {
        for (Client &client : m_clients) {
            if (client.fd == -1 || client.evdev != evdev || client.down == down) {
                continue;
            }
            client.down = down;
            KeyEvent message{};
            message.down = down ? 1 : 0;
            message.monotonicUsec = monotonicUsec();
            if (write(client.fd, &message, sizeof(message)) != sizeof(message)) {
                dropClient(client);
            }
        }
    }

    int m_listenFd;
    std::vector<Keyboard> m_keyboards;
    int m_epoll = -1;
    int m_inotify = -1;
    std::array<Client, maxClients> m_clients{};
    std::array<RateBucket, maxClients * 2> m_rate{};
    int m_clientCount = 0;
};

} // namespace

int main()
{
    if (geteuid() == 0) {
        logLine("refusing to run as root; the unit must set User=speecher-keywatch");
        return 1;
    }
    const int listenCount = sd_listen_fds(0);
    if (listenCount != 1) {
        logLine("expected exactly one socket-activation fd from systemd");
        return 1;
    }

    Server server(SD_LISTEN_FDS_START);
    if (!server.openKeyboards()) {
        return 1;
    }
    return server.run();
}
