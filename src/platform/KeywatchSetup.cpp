#include "platform/KeywatchSetup.h"

#include "output/HelperInstall.h"
#include "setup/KeywatchProtocol.h"

#include <QFileInfo>
#include <QLocalSocket>
#include <QThreadPool>

#include <atomic>
#include <chrono>
#include <cstdint>

#include <unistd.h>

namespace speecher {
namespace {

constexpr auto socketUnitPath = "/etc/systemd/system/speecher-keywatchd.socket";
constexpr auto daemonFileName = "speecher-keywatchd";
constexpr auto policyFileName = "speecher_keywatchd.cil";
constexpr int connectTimeoutMs = 500;
constexpr int replyTimeoutMs = 2000;
// The daemon refuses more than eight connections a minute per user, and the
// binder's recovery poll probes every second. Keep a probe's answer this long
// so watching the status never costs the budget a real watch needs.
constexpr int answerCacheMs = 20000;
#ifndef SPEECHER_KEYWATCH_HELPER_PATH
#define SPEECHER_KEYWATCH_HELPER_PATH "/usr/libexec/speecher/speecher-keywatch-setup"
#endif

// The probe runs on the UI thread while the exchange with the daemon and the
// pkexec'd install run on workers, so the cache is atomics rather than a timer
// object.
std::atomic<std::int64_t> cachedAnswerAtMs{0};
std::atomic<bool> cachedAnswer{false};
std::atomic<bool> cachedProtocolMatch{false};
std::atomic<bool> askInFlight{false};

std::int64_t monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Key id 0 is not in the permitted table, so a live daemon answers this with
// KeyNotPermitted without taking a watch or disturbing one already running. A
// daemon that failed to start leaves systemd's socket accepting and then
// closing, which shows up here as no answer. The reply's contents matter too:
// an installed daemon from before a protocol bump answers BadVersion with its
// own version number, which is alive but unusable.
bool askDaemon(bool &protocolMatches)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(keywatch::socketPath));
    if (!socket.waitForConnected(connectTimeoutMs)) {
        return false;
    }
    const keywatch::WatchRequest ping{keywatch::protocolVersion, 0};
    socket.write(reinterpret_cast<const char *>(&ping), sizeof(ping));
    socket.flush();
    while (socket.bytesAvailable() < qint64(sizeof(keywatch::WatchReply))) {
        if (!socket.waitForReadyRead(replyTimeoutMs)) {
            return false;
        }
    }
    keywatch::WatchReply reply{};
    socket.read(reinterpret_cast<char *>(&reply), sizeof(reply));
    protocolMatches = reply.version == keywatch::protocolVersion
        && reply.refusal != std::uint8_t(keywatch::Refusal::BadVersion);
    return true;
}

// A daemon that accepted the connection and then died costs this exchange
// seconds, and every caller of probe() is on the GUI thread, so the exchange
// runs on a pool thread and the caller reads the answer it left behind.
void askDaemonInBackground()
{
    bool idle = false;
    if (!askInFlight.compare_exchange_strong(idle, true)) {
        return;
    }
    // Create the notifier here so it belongs to the thread that probes, not to
    // whichever pool thread happens to finish the exchange.
    KeywatchSetup::daemonAnswer();
    QThreadPool::globalInstance()->start([] {
        bool protocolMatches = false;
        const bool answered = askDaemon(protocolMatches);
        cachedAnswer.store(answered);
        cachedProtocolMatch.store(protocolMatches);
        cachedAnswerAtMs.store(monotonicMs());
        askInFlight.store(false);
        QMetaObject::invokeMethod(KeywatchSetup::daemonAnswer(),
                                  "changed",
                                  Qt::QueuedConnection);
    });
}

bool daemonAnswers()
{
    const std::int64_t askedAt = cachedAnswerAtMs.load();
    if (askedAt == 0 || monotonicMs() - askedAt >= answerCacheMs) {
        askDaemonInBackground();
    }
    return cachedAnswer.load();
}

} // namespace

KeywatchSetupStatus KeywatchSetup::evaluate(const KeywatchProbeFacts &facts)
{
    if (!facts.socketUnitInstalled) {
        return {KeywatchSetupState::NotInstalled,
                QStringLiteral("Not installed"),
                QStringLiteral("The key helper is not installed.")};
    }
    if (!facts.socketExists) {
        return {KeywatchSetupState::DaemonNotRunning,
                QStringLiteral("Installed, not running"),
                QStringLiteral("The key helper's socket is not active.")};
    }
    if (!facts.socketWritable) {
        return {KeywatchSetupState::DaemonNotRunning,
                QStringLiteral("Installed, not reachable"),
                QStringLiteral("The key helper's socket cannot be reached.")};
    }
    if (!facts.daemonAnswers) {
        return {KeywatchSetupState::DaemonNotRunning,
                QStringLiteral("Installed, not answering"),
                QStringLiteral("The key helper is installed but does not answer. "
                               "Set it up again to repair it.")};
    }
    if (!facts.daemonProtocolMatches) {
        return {KeywatchSetupState::NeedsReinstall,
                QStringLiteral("Installed, needs an update"),
                QStringLiteral("The installed key helper is from another version of "
                               "Speecher. Set it up again to update it.")};
    }
    return {KeywatchSetupState::Ready,
            QStringLiteral("Ready"),
            QStringLiteral("The key helper is ready.")};
}

KeywatchSetupStatus KeywatchSetup::probe()
{
    KeywatchProbeFacts facts;
    facts.socketUnitInstalled = QFileInfo::exists(QLatin1StringView(socketUnitPath));
    const QFileInfo socket(QLatin1StringView(keywatch::socketPath));
    facts.socketExists = socket.exists();
    // QFileInfo::isWritable() answers from the owner's point of view for a
    // socket node; access() asks the kernel about this process.
    facts.socketWritable = facts.socketExists && access(keywatch::socketPath, W_OK) == 0;
    facts.daemonAnswers = facts.socketWritable && daemonAnswers();
    facts.daemonProtocolMatches = facts.daemonAnswers && cachedProtocolMatch.load();
    return evaluate(facts);
}

KeywatchDaemonAnswer *KeywatchSetup::daemonAnswer()
{
    // Outlives the application object on purpose: a pool thread may still be
    // waiting on the daemon when the GUI tears down.
    static KeywatchDaemonAnswer *const answer = new KeywatchDaemonAnswer;
    return answer;
}

void KeywatchSetup::forgetDaemonAnswer()
{
    cachedAnswerAtMs.store(0);
    cachedAnswer.store(false);
    cachedProtocolMatch.store(false);
}

bool KeywatchSetup::install(QString *error)
{
    const bool installed = helpers::runSetupHelper(SPEECHER_KEYWATCH_HELPER_PATH,
                                                   {QLatin1StringView(daemonFileName),
                                                    QLatin1StringView(policyFileName)},
                                                   helpers::HelperAction::Install,
                                                   error);
    forgetDaemonAnswer();
    return installed;
}

bool KeywatchSetup::remove(QString *error)
{
    const bool removed = helpers::runSetupHelper(SPEECHER_KEYWATCH_HELPER_PATH,
                                                 {QLatin1StringView(daemonFileName),
                                                  QLatin1StringView(policyFileName)},
                                                 helpers::HelperAction::Remove,
                                                 error);
    forgetDaemonAnswer();
    return removed;
}

} // namespace speecher
