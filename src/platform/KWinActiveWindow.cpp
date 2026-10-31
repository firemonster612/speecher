#include "platform/KWinActiveWindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QStandardPaths>

#include <utility>

namespace speecher {

namespace {

constexpr auto kKWinService = "org.kde.KWin";
constexpr auto kScriptingPath = "/Scripting";
constexpr auto kScriptingInterface = "org.kde.kwin.Scripting";
constexpr auto kProbeInterface = "org.speecher.WindowProbe";

// One script, both KWin generations: Plasma 6 exposes workspace.activeWindow,
// Plasma 5 workspace.activeClient. Everything is stringified so D-Bus never has
// to guess a number's width, and the pid lets the caller confirm the window is
// not Speecher's own surface. internalId is KWin's per-window handle; a
// compositor that does not have it reports an empty string rather than failing.
// probe comes back with the report, so a late one from an earlier probe is
// told apart.
QString scriptSource(const QString &service, const QString &path, quint64 probe)
{
    return QStringLiteral(
               "var w = workspace.activeWindow || workspace.activeClient;\n"
               "var cls = '', name = '', cap = '', pid = '0', id = '';\n"
               "if (w) {\n"
               "  cls = w.resourceClass ? '' + w.resourceClass : '';\n"
               "  name = w.resourceName ? '' + w.resourceName : '';\n"
               "  cap = w.caption ? '' + w.caption : '';\n"
               "  pid = w.pid ? '' + w.pid : '0';\n"
               "  id = w.internalId ? '' + w.internalId : '';\n"
               "}\n"
               "callDBus('%1', '%2', '%3', 'reportWindow', cls, name, cap, pid, id, '%4');\n")
        .arg(service, path, QString::fromLatin1(kProbeInterface), QString::number(probe));
}

bool kwinAvailable()
{
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QString::fromLatin1(kKWinService));
}

// KWin only loads scripts from a file path (there is no loadScriptFromText on
// the shipping interface), so the templated source is written to a runtime file
// KWin reads back. Only the private runtime directory will do: a fixed name in
// a shared one such as /tmp lets another account plant the script KWin runs.
QString writeScriptFile(const QString &source)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) {
        return {};
    }
    const QString path = dir + QStringLiteral("/speecher-kwin-active-window.js");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(source.toUtf8());
    file.close();
    return path;
}

// KWin's answer is the script's id, or a negative one when it turned the
// script down.
QDBusPendingCall loadKWinScript(const QString &scriptPath)
{
    QDBusMessage load = QDBusMessage::createMethodCall(
        QString::fromLatin1(kKWinService),
        QString::fromLatin1(kScriptingPath),
        QString::fromLatin1(kScriptingInterface),
        QStringLiteral("loadScript"));
    load.setArguments({scriptPath, QStringLiteral("speecher-active-window")});
    return QDBusConnection::sessionBus().asyncCall(load, 500);
}

void runKWinScript(int scriptId)
{
    const QString path = QStringLiteral("/Scripting/Script%1").arg(scriptId);
    QDBusMessage run = QDBusMessage::createMethodCall(
        QString::fromLatin1(kKWinService), path,
        QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    QDBusConnection::sessionBus().call(run, QDBus::NoBlock);
}

void unloadKWinScript()
{
    QDBusMessage unload = QDBusMessage::createMethodCall(
        QString::fromLatin1(kKWinService),
        QString::fromLatin1(kScriptingPath),
        QString::fromLatin1(kScriptingInterface),
        QStringLiteral("unloadScript"));
    unload.setArguments({QStringLiteral("speecher-active-window")});
    QDBusConnection::sessionBus().call(unload, QDBus::NoBlock, 500);
}

} // namespace

KWinActiveWindow::KWinActiveWindow(QObject *parent)
    : QObject(parent)
    , m_objectPath(QStringLiteral("/org/speecher/WindowProbe"))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { answer(std::nullopt); });
}

KWinActiveWindow::~KWinActiveWindow()
{
    // Gone mid-probe: the script must not stay loaded. One still loading is
    // unloaded by the next probe, whichever process runs it.
    if (!m_waiting.isEmpty()) {
        unloadKWinScript();
    }
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject(m_objectPath);
    }
}

bool KWinActiveWindow::ensureRegistered()
{
    if (m_registered) {
        return true;
    }
    m_registered = QDBusConnection::sessionBus().registerObject(
        m_objectPath, this, QDBusConnection::ExportScriptableSlots);
    return m_registered;
}

void KWinActiveWindow::reportWindow(const QString &resourceClass,
                                    const QString &resourceName,
                                    const QString &caption,
                                    const QString &processId,
                                    const QString &internalId,
                                    const QString &probe)
{
    // A probe that timed out can still report, while the next one waits.
    if (m_waiting.isEmpty() || probe != QString::number(m_probes)) {
        return;
    }
    const KWinWindowInfo window{resourceClass.trimmed(), resourceName.trimmed(), caption.trimmed(),
                                internalId.trimmed(), processId.toLongLong()};
    // Never report Speecher's own surface: pasting into ourselves is never the
    // intent, and the popup can momentarily read as active on some setups.
    if (window.resourceClass.isEmpty() || window.processId == QCoreApplication::applicationPid()) {
        answer(std::nullopt);
        return;
    }
    answer(window);
}

void KWinActiveWindow::activeWindow(std::function<void(const std::optional<KWinWindowInfo> &)> done, int timeoutMs)
{
    if (!kwinAvailable() || !ensureRegistered()) {
        done(std::nullopt);
        return;
    }
    m_waiting << std::move(done);
    if (m_waiting.size() > 1) {
        return;
    }

    const quint64 probe = ++m_probes;
    const QString scriptPath = writeScriptFile(
        scriptSource(QDBusConnection::sessionBus().baseService(), m_objectPath, probe));
    if (scriptPath.isEmpty()) {
        answer(std::nullopt);
        return;
    }
    // KWin refuses to load a plugin name it already has, and a probe that
    // never got to unload (Speecher was killed mid-probe) leaves its script
    // loaded for the rest of the KWin session.
    unloadKWinScript();
    auto *loading = new QDBusPendingCallWatcher(loadKWinScript(scriptPath), this);
    connect(loading, &QDBusPendingCallWatcher::finished, this,
            [this, probe, timeoutMs](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (probe != m_probes || m_waiting.isEmpty()) {
            return;
        }
        const QDBusPendingReply<int> scriptId = *watcher;
        if (scriptId.isError() || scriptId.value() < 0) {
            answer(std::nullopt);
            return;
        }
        // reportWindow arrives once the script has run. The load has its own
        // limit; this one is KWin's to run the script and report.
        m_timeout.start(timeoutMs);
        runKWinScript(scriptId.value());
    });
}

void KWinActiveWindow::answer(const std::optional<KWinWindowInfo> &window)
{
    m_timeout.stop();
    unloadKWinScript();
    // Taken first: an answer may ask again, which starts a probe of its own.
    const auto waiting = std::exchange(m_waiting, {});
    for (const auto &done : waiting) {
        done(window);
    }
}

} // namespace speecher
