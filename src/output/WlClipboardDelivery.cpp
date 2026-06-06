#include "output/WlClipboardDelivery.h"

#include <QProcess>
#include <QStandardPaths>

namespace speecher {

WlClipboardDelivery::WlClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool WlClipboardDelivery::isAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("wl-copy")).isEmpty();
}

bool WlClipboardDelivery::copy(const QString &text, QString *error)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("wl-copy is not installed");
        }
        return false;
    }

    QProcess process;
    process.start(executable, {QStringLiteral("--type"), QStringLiteral("text/plain")});
    if (!process.waitForStarted(1000)) {
        if (error) {
            *error = QStringLiteral("Could not start wl-copy");
        }
        return false;
    }

    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (process.waitForBytesWritten(1000) && process.waitForFinished(5000)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return true;
    }

    process.kill();
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (error) {
        *error = stderrText.isEmpty() ? QStringLiteral("wl-copy failed") : QStringLiteral("wl-copy failed: %1").arg(stderrText);
    }
    return false;
}

} // namespace speecher
