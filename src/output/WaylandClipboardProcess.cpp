#include "output/WaylandClipboardProcess.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace speecher::WaylandClipboardProcess {

namespace {

constexpr int processStartTimeoutMs = 1000;
QString processErrorMessage(const QString &tool, QProcess &process, const QString &fallback)
{
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return stderrText.isEmpty() ? fallback : QStringLiteral("%1 failed: %2").arg(tool, stderrText);
}

} // namespace

QString wlCopyExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
}

QString wlPasteExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
}

QString helperExecutable()
{
    const QString adjacent = QCoreApplication::applicationDirPath()
        + QStringLiteral("/speecher-wayland-clipboard");
    if (QFileInfo::exists(adjacent) && QFileInfo(adjacent).isExecutable()) {
        return adjacent;
    }
#ifdef SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH
    const QString installed = QStringLiteral(SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH);
    if (QFileInfo::exists(installed) && QFileInfo(installed).isExecutable()) {
        return installed;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("speecher-wayland-clipboard"));
}

bool run(const QString &executable,
         const QString &tool,
         const QStringList &arguments,
         const QByteArray *input,
         QByteArray *output,
         QString *error,
         int timeoutMs,
         qsizetype maximumOutputBytes)
{
    QProcess process;
    if (!output) {
        process.setStandardOutputFile(QProcess::nullDevice());
    }
    process.start(executable, arguments);
    if (!process.waitForStarted(processStartTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(tool);
        }
        return false;
    }

    if (input) {
        process.write(*input);
        if (!input->isEmpty() && !process.waitForBytesWritten(processStartTimeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            if (error) {
                *error = QStringLiteral("%1 did not accept clipboard data").arg(tool);
            }
            return false;
        }
    }
    process.closeWriteChannel();

    QByteArray capturedOutput;
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning && elapsed.elapsed() < timeoutMs) {
        process.waitForReadyRead(qMin(50, timeoutMs - int(elapsed.elapsed())));
        if (output) {
            capturedOutput += process.readAllStandardOutput();
            if (capturedOutput.size() > maximumOutputBytes) {
                process.kill();
                process.waitForFinished(1000);
                if (error) {
                    *error = QStringLiteral("%1 returned too much clipboard data").arg(tool);
                }
                return false;
            }
        }
    }
    if (output) {
        capturedOutput += process.readAllStandardOutput();
    }
    if (capturedOutput.size() > maximumOutputBytes) {
        if (error) {
            *error = QStringLiteral("%1 returned too much clipboard data").arg(tool);
        }
        return false;
    }
    if (process.state() == QProcess::NotRunning
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        if (output) {
            *output = capturedOutput;
        }
        return true;
    }

    process.kill();
    process.waitForFinished(1000);
    if (error) {
        *error = elapsed.elapsed() >= timeoutMs
            ? QStringLiteral("%1 timed out").arg(tool)
            : processErrorMessage(tool, process, QStringLiteral("%1 failed").arg(tool));
    }
    return false;
}

bool looksLikeEmptyClipboardError(const QString &message)
{
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("nothing is copied"))
        || lower.contains(QStringLiteral("clipboard is empty"))
        || lower.contains(QStringLiteral("no selection"))
        || lower.contains(QStringLiteral("no data"));
}

} // namespace speecher::WaylandClipboardProcess
