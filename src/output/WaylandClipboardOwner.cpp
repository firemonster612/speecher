#include "output/WaylandClipboardOwner.h"

#include "output/WaylandClipboardProcess.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>

namespace speecher {

namespace {

constexpr int processStartTimeoutMs = 1000;
constexpr int ownerReadyTimeoutMs = 3000;

QPointer<QProcess> activeClipboardOwner;

void stopClipboardOwner(QProcess *process)
{
    if (!process) {
        return;
    }
    if (process->state() != QProcess::NotRunning) {
        if (!process->waitForFinished(500)) {
            process->terminate();
            if (!process->waitForFinished(500)) {
                process->kill();
                process->waitForFinished(500);
            }
        }
    }
    delete process;
}

void stopActiveClipboardOwner()
{
    QProcess *process = activeClipboardOwner;
    activeClipboardOwner = nullptr;
    stopClipboardOwner(process);
}

QByteArray ownerPayload(const QList<ClipboardMimePart> &parts)
{
    QJsonArray encodedParts;
    for (const ClipboardMimePart &part : parts) {
        encodedParts.append(QJsonObject{
            {QStringLiteral("mime"), part.mimeType},
            {QStringLiteral("data"), QString::fromLatin1(part.data.toBase64())},
        });
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("parts"), encodedParts},
    }).toJson(QJsonDocument::Compact);
}

} // namespace

bool WaylandClipboardOwner::start(const QList<ClipboardMimePart> &parts, QString *error)
{
    static const bool cleanupRegistered = [] {
        qAddPostRoutine(stopActiveClipboardOwner);
        return true;
    }();
    Q_UNUSED(cleanupRegistered);

    const QString executable = WaylandClipboardProcess::helperExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Speecher's Wayland clipboard helper is unavailable");
        }
        return false;
    }

    auto *process = new QProcess;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start(executable);
    if (!process->waitForStarted(processStartTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("Could not start Speecher's Wayland clipboard helper");
        }
        delete process;
        return false;
    }

    process->write(ownerPayload(parts));
    process->closeWriteChannel();
    if (!process->waitForReadyRead(ownerReadyTimeoutMs)
        || process->readLine().trimmed() != QByteArrayLiteral("READY")) {
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->kill();
        process->waitForFinished(1000);
        if (error) {
            *error = stderrText.isEmpty()
                ? QStringLiteral("Wayland clipboard helper did not publish the selection")
                : stderrText;
        }
        delete process;
        return false;
    }

    QProcess *previousOwner = activeClipboardOwner;
    activeClipboardOwner = nullptr;
    stopClipboardOwner(previousOwner);
    if (!parts.isEmpty()) {
        activeClipboardOwner = process;
    } else {
        process->waitForFinished(1000);
        delete process;
    }
    return true;
}

} // namespace speecher
