#include "output/WtypeDelivery.h"

#include <QProcess>
#include <QStandardPaths>

namespace speecher {

WtypeDelivery::WtypeDelivery(QObject *parent)
    : QObject(parent)
{
}

bool WtypeDelivery::isAvailable(const QString &command)
{
    return !QStandardPaths::findExecutable(command).isEmpty();
}

bool WtypeDelivery::deliver(const QString &command, const QString &text, QString *error)
{
    const QString executable = QStandardPaths::findExecutable(command);
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("%1 is not installed").arg(command);
        }
        return false;
    }

    QProcess process;
    process.start(executable, {QStringLiteral("--"), text});
    if (!process.waitForStarted(1000)) {
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(command);
        }
        return false;
    }
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        if (error) {
            *error = QStringLiteral("%1 failed").arg(command);
        }
        return false;
    }
    return true;
}

} // namespace speecher
