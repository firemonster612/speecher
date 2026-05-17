#include "app/ApplicationController.h"
#include "app/SingleInstanceIpc.h"
#include "core/SettingsStore.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <iostream>

using namespace speecher;

static QFile *g_logFile = nullptr;
static QMutex g_logMutex;

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QMutexLocker locker(&g_logMutex);
    if (!g_logFile || !g_logFile->isOpen()) {
        return;
    }
    const char *level = "info";
    if (type == QtDebugMsg) {
        level = "debug";
    } else if (type == QtWarningMsg) {
        level = "warning";
    } else if (type == QtCriticalMsg) {
        level = "critical";
    } else if (type == QtFatalMsg) {
        level = "fatal";
    }
    QTextStream stream(g_logFile);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << ' ' << level << ' ' << message << '\n';
    stream.flush();
}

static QString installLogHandler()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.cache/speecher");
    }
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/speecher.log");
    g_logFile = new QFile(path);
    g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "speecher log started path=" + path;
    return path;
}

static bool startDetachedToggle()
{
    QString program = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (program.isEmpty()) {
        program = QCoreApplication::applicationFilePath();
    }
    return QProcess::startDetached(program, {QStringLiteral("--daemon"), QStringLiteral("--start-listening")});
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("speecher"));
    QApplication::setOrganizationName(QStringLiteral("local.speecher"));
    Theme::apply(SettingsStore().theme());
    const QString logPath = installLogHandler();

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--version"))) {
        std::cout << "speecher " << SPEECHER_VERSION << "\n";
        std::cout << "log " << logPath.toStdString() << "\n";
        return 0;
    }

    const bool toggleCli = args.size() >= 2 && args.at(1) == QStringLiteral("toggle");
    const bool daemon = args.contains(QStringLiteral("--daemon"));
    const bool startListening = args.contains(QStringLiteral("--start-listening"));

    if (toggleCli) {
        IpcResponse response;
        if (SingleInstanceIpc::sendCommand(QStringLiteral("toggle"), &response)) {
            std::cout << response.state.toStdString() << "\n";
            return response.ok ? 0 : 1;
        }
        if (!startDetachedToggle()) {
            std::cerr << "Could not start speecher daemon\n";
            return 1;
        }
        return 0;
    }

    ApplicationController controller(daemon);
    QString ipcError;
    if (!controller.startIpc(&ipcError)) {
        if (!daemon && SingleInstanceIpc::sendCommand(QStringLiteral("showMain"), nullptr)) {
            return 0;
        }
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if (startListening) {
        QTimer::singleShot(0, &controller, &ApplicationController::startListening);
    }
    if (!daemon) {
        controller.showMainWindow();
    }
    return app.exec();
}
