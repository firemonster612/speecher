#include "output/ClipboardDelivery.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace speecher {

ClipboardDelivery::ClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardDelivery::copy(const QString &text, QString *error)
{
    QString wlCopyError;
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
    if (!executable.isEmpty()) {
        QProcess process;
        process.start(executable, {QStringLiteral("--type"), QStringLiteral("text/plain")});
        if (!process.waitForStarted(1000)) {
            wlCopyError = QStringLiteral("Could not start wl-copy");
        } else {
            process.write(text.toUtf8());
            process.closeWriteChannel();
            if (process.waitForBytesWritten(1000) && process.waitForFinished(5000)
                && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
                return true;
            }

            process.kill();
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            wlCopyError = stderrText.isEmpty() ? QStringLiteral("wl-copy failed") : QStringLiteral("wl-copy failed: %1").arg(stderrText);
        }
    } else {
        wlCopyError = QStringLiteral("wl-copy is not installed");
    }

    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        if (error) {
            *error = wlCopyError.isEmpty() ? QStringLiteral("Clipboard is unavailable") : wlCopyError;
        }
        return false;
    }
    constexpr int maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        clipboard->setText(text);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (attempt + 1 < maxAttempts) {
            QThread::msleep(40);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    return true;
}

} // namespace speecher
