#include "platform/KeywatchShortcutBinder.h"

#include "platform/KeywatchSetup.h"
#include "setup/KeywatchProtocol.h"

#include <QLocalSocket>
#include <QSignalBlocker>
#include <QTimer>

namespace speecher {
namespace {

constexpr int reconnectDelayMs = 1000;
// After the daemon refused a reconnect, retry below its per-uid rate limit.
constexpr int refusedRetryDelayMs = 10000;
constexpr int replyTimeoutMs = 2000;
constexpr int recoveryPollMs = 1000;

const char *refusalText(keywatch::Refusal refusal)
{
    switch (refusal) {
    case keywatch::Refusal::None: return "accepted";
    case keywatch::Refusal::BadVersion: return "the helper speaks another protocol version";
    case keywatch::Refusal::KeyNotPermitted: return "the helper does not permit that key";
    case keywatch::Refusal::AlreadyWatching: return "a key is already being watched for this user";
    case keywatch::Refusal::TooManyRequests: return "too many requests; try again in a minute";
    }
    return "unknown reason";
}

} // namespace

KeywatchShortcutBinder::KeywatchShortcutBinder(QObject *parent)
    : SingleKeyShortcutBinder(parent)
    , m_socket(new QLocalSocket(this))
{
    // The async slots serve reconnects after the daemon restarts; the initial
    // exchange in watch() is synchronous and runs with these signals blocked.
    connect(m_socket, &QLocalSocket::connected, this, [this] {
        const keywatch::WatchRequest request{keywatch::protocolVersion, m_keyId};
        m_replied = false;
        m_socket->write(reinterpret_cast<const char *>(&request), sizeof(request));
    });
    connect(m_socket, &QLocalSocket::readyRead, this, [this] { readFromDaemon(); });
    connect(m_socket, &QLocalSocket::disconnected, this,
            [this] { reconnectLater(reconnectDelayMs); });
    connect(m_socket, &QLocalSocket::errorOccurred, this,
            [this] { reconnectLater(reconnectDelayMs); });
}

bool KeywatchShortcutBinder::supported() const
{
    return KeywatchSetup::probe().ready();
}

QString KeywatchShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    const QString reason = SingleKeyShortcutBinder::unsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        return reason;
    }
    if (!keywatch::permittedKeyByCode(binding.keyCode().toStdString())) {
        return QStringLiteral(
            "On Wayland, Speecher's key helper watches only keys that cannot type text: "
            "Shift, Ctrl, Alt, Meta, Caps Lock and F13 to F24. %1 is not one of them.")
            .arg(binding.displayText());
    }
    const KeywatchSetupStatus status = KeywatchSetup::probe();
    return status.ready() ? QString() : status.detail;
}

// A stored key that could not bind (the helper not installed yet, or this
// session not in its group) starts a probe poll, as the mac binder polls its
// Accessibility grant: installing the helper later revives the shortcut
// without a restart.
void KeywatchShortcutBinder::bind()
{
    SingleKeyShortcutBinder::bind();
    if (shortcut().isSingleKey()) {
        if (m_recoveryPoll) {
            m_recoveryPoll->stop();
        }
        return;
    }
    if (storedBinding().isSingleKey()) {
        startRecoveryPoll();
    }
}

void KeywatchShortcutBinder::startRecoveryPoll()
{
    if (!m_recoveryPoll) {
        m_recoveryPoll = new QTimer(this);
        m_recoveryPoll->setInterval(recoveryPollMs);
        connect(m_recoveryPoll, &QTimer::timeout, this, [this] {
            if (!KeywatchSetup::probe().ready()) {
                return;
            }
            SingleKeyShortcutBinder::bind();
            if (shortcut().isSingleKey()) {
                m_recoveryPoll->stop();
                emit supportChanged();
            }
        });
    }
    m_recoveryPoll->start();
}

// The synchronous exchange with the daemon: its OK is what commits the
// binding. A shortcut must never look bound while the helper refused it or
// never answered (design property 9), so a refusal comes back as the error
// the UI shows instead of a stored binding.
QString KeywatchShortcutBinder::watch(const PhysicalKey &key)
{
    unwatch();
    m_keyId = keywatch::permittedKeyByCode(key.code)->id;
    const QSignalBlocker blocker(m_socket);
    m_socket->connectToServer(QString::fromLatin1(keywatch::socketPath));
    if (!m_socket->waitForConnected(replyTimeoutMs)) {
        unwatch();
        const KeywatchSetupStatus status = KeywatchSetup::probe();
        return status.ready() ? QStringLiteral("Speecher could not reach the key helper.")
                              : status.detail;
    }
    const keywatch::WatchRequest request{keywatch::protocolVersion, m_keyId};
    m_socket->write(reinterpret_cast<const char *>(&request), sizeof(request));
    while (m_socket->bytesAvailable() < qint64(sizeof(keywatch::WatchReply))) {
        if (!m_socket->waitForReadyRead(replyTimeoutMs)) {
            unwatch();
            return QStringLiteral("The key helper did not answer.");
        }
    }
    keywatch::WatchReply reply{};
    m_socket->read(reinterpret_cast<char *>(&reply), sizeof(reply));
    if (reply.refusal != quint8(keywatch::Refusal::None)) {
        const QString reason = QString::fromLatin1(refusalText(keywatch::Refusal(reply.refusal)));
        unwatch();
        return QStringLiteral("The key helper refused the watch: %1.").arg(reason);
    }
    m_replied = true;
    // A KeyEvent that landed in the same readyRead round as the reply is
    // already signalled, so Qt will not signal it again; drain it now or the
    // press is only processed when the next event arrives.
    readFromDaemon();
    return QString();
}

void KeywatchShortcutBinder::unwatch()
{
    m_keyId = 0;
    m_replied = false;
    m_socket->abort();
}

void KeywatchShortcutBinder::reconnectLater(int delayMs)
{
    if (m_keyId == 0 || m_reconnectPending) {
        return;
    }
    keyUp();
    m_reconnectPending = true;
    QTimer::singleShot(delayMs, this, [this] {
        m_reconnectPending = false;
        if (m_keyId != 0 && m_socket->state() == QLocalSocket::UnconnectedState) {
            m_socket->connectToServer(QString::fromLatin1(keywatch::socketPath));
        }
    });
}

void KeywatchShortcutBinder::readFromDaemon()
{
    if (!m_replied) {
        if (m_socket->bytesAvailable() < qint64(sizeof(keywatch::WatchReply))) {
            return;
        }
        keywatch::WatchReply reply{};
        m_socket->read(reinterpret_cast<char *>(&reply), sizeof(reply));
        m_replied = true;
        if (reply.refusal != quint8(keywatch::Refusal::None)) {
            // A refusal on reconnect. The binding stays: silently unbinding
            // would leave a shortcut that looks bound and never fires, so
            // keep retrying at a pace the daemon's rate limit allows.
            qWarning("The key helper refused the watch: %s",
                     refusalText(keywatch::Refusal(reply.refusal)));
            // Scheduled first: abort() emits the disconnect signals, whose
            // handler must find the slow retry already pending.
            reconnectLater(refusedRetryDelayMs);
            m_socket->abort();
            return;
        }
    }
    while (m_socket->bytesAvailable() >= qint64(sizeof(keywatch::KeyEvent))) {
        keywatch::KeyEvent event{};
        m_socket->read(reinterpret_cast<char *>(&event), sizeof(event));
        event.down ? keyDown() : keyUp();
    }
}

} // namespace speecher
