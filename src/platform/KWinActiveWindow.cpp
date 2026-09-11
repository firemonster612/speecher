#include "platform/KWinActiveWindow.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>

namespace speecher {

namespace {

constexpr auto kKWinService = "org.kde.KWin";
constexpr auto kScriptingPath = "/Scripting";
constexpr auto kScriptingInterface = "org.kde.kwin.Scripting";
constexpr auto kProbeInterface = "org.speecher.WindowProbe";

// One script, both KWin generations: Plasma 6 exposes workspace.activeWindow,
// Plasma 5 workspace.activeClient. Everything is stringified so D-Bus never has
// to guess a number's width, and the pid lets the caller confirm the window is
// not Speecher's own surface.
QString scriptSource(const QString &service, const QString &path)
{
    return QStringLiteral(
               "var w = workspace.activeWindow || workspace.activeClient;\n"
               "var cls = '', name = '', cap = '', pid = '0';\n"
               "if (w) {\n"
               "  cls = w.resourceClass ? '' + w.resourceClass : '';\n"
               "  name = w.resourceName ? '' + w.resourceName : '';\n"
               "  cap = w.caption ? '' + w.caption : '';\n"
               "  pid = w.pid ? '' + w.pid : '0';\n"
               "}\n"
               "callDBus('%1', '%2', '%3', 'reportWindow', cls, name, cap, pid);\n")
        .arg(service, path, QString::fromLatin1(kProbeInterface));
}

bool kwinAvailable()
{
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QString::fromLatin1(kKWinService));
}

// KWin only loads scripts from a file path (there is no loadScriptFromText on
// the shipping interface), so the templated source is written to a runtime file
// KWin reads back. The path doubles as the plugin name for unloading.
QString writeScriptFile(const QString &source)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) {
        dir = QDir::tempPath();
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

int loadKWinScript(const QString &scriptPath)
{
    QDBusMessage load = QDBusMessage::createMethodCall(
        QString::fromLatin1(kKWinService),
        QString::fromLatin1(kScriptingPath),
        QString::fromLatin1(kScriptingInterface),
        QStringLiteral("loadScript"));
    load.setArguments({scriptPath, QStringLiteral("speecher-active-window")});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(load, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return -1;
    }
    return reply.arguments().first().toInt();
}

void runKWinScript(int scriptId)
{
    const QString path = QStringLiteral("/Scripting/Script%1").arg(scriptId);
    QDBusMessage run = QDBusMessage::createMethodCall(
        QString::fromLatin1(kKWinService), path,
        QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    QDBusConnection::sessionBus().call(run, QDBus::Block, 500);
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
}

KWinActiveWindow::~KWinActiveWindow()
{
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
                                    const QString &processId)
{
    m_pending = {resourceClass.trimmed(), resourceName.trimmed(), caption.trimmed(),
                 processId.toLongLong()};
    m_reported = true;
}

std::optional<KWinWindowInfo> KWinActiveWindow::activeWindow(int timeoutMs)
{
    if (!kwinAvailable() || !ensureRegistered()) {
        return std::nullopt;
    }

    m_reported = false;
    m_pending = {};

    const QString scriptPath = writeScriptFile(
        scriptSource(QDBusConnection::sessionBus().baseService(), m_objectPath));
    if (scriptPath.isEmpty()) {
        return std::nullopt;
    }
    const int scriptId = loadKWinScript(scriptPath);
    if (scriptId < 0) {
        return std::nullopt;
    }
    runKWinScript(scriptId);
    const auto unload = qScopeGuard(&unloadKWinScript);

    if (!m_reported) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        // reportWindow arrives on the D-Bus queue; poll it into the loop.
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [this, &loop] {
            if (m_reported) {
                loop.quit();
            }
        });
        timeout.start(timeoutMs);
        poll.start(5);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }

    if (!m_reported || m_pending.resourceClass.isEmpty()) {
        return std::nullopt;
    }
    // Never report Speecher's own surface: pasting into ourselves is never the
    // intent, and the popup can momentarily read as active on some setups.
    if (m_pending.processId == QCoreApplication::applicationPid()) {
        return std::nullopt;
    }
    return m_pending;
}

} // namespace speecher
