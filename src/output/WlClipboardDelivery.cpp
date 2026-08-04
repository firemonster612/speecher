#include "output/WlClipboardDelivery.h"

#include "output/DeliveryContent.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>

namespace speecher {

namespace {

constexpr int clipboardProcessStartTimeoutMs = 1000;
constexpr int clipboardProcessTimeoutMs = 5000;
constexpr int clipboardOwnerReadyTimeoutMs = 3000;

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

QString wlCopyExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
}

QString wlPasteExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
}

QString clipboardHelperExecutable()
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

QString processErrorMessage(const QString &tool, QProcess &process, const QString &fallback)
{
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return stderrText.isEmpty() ? fallback : QStringLiteral("%1 failed: %2").arg(tool, stderrText);
}

bool runClipboardProcess(const QString &executable,
                         const QString &tool,
                         const QStringList &arguments,
                         const QByteArray *input,
                         QByteArray *output,
                         QString *error)
{
    QProcess process;
    process.start(executable, arguments);
    if (!process.waitForStarted(clipboardProcessStartTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("Could not start %1").arg(tool);
        }
        return false;
    }

    if (input) {
        process.write(*input);
        if (!input->isEmpty() && !process.waitForBytesWritten(clipboardProcessStartTimeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            if (error) {
                *error = QStringLiteral("%1 did not accept clipboard data").arg(tool);
            }
            return false;
        }
    }
    process.closeWriteChannel();

    if (process.waitForFinished(clipboardProcessTimeoutMs)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        if (output) {
            *output = process.readAllStandardOutput();
        }
        return true;
    }

    process.kill();
    process.waitForFinished(1000);
    if (error) {
        *error = processErrorMessage(tool, process, QStringLiteral("%1 failed").arg(tool));
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

QString preferredMimeType(const QStringList &mimeTypes)
{
    const QStringList preferred{
        QStringLiteral("text/plain;charset=utf-8"),
        QStringLiteral("text/plain"),
        QStringLiteral("UTF8_STRING"),
        QStringLiteral("text/html"),
        QStringLiteral("text/uri-list"),
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/jpg"),
    };
    for (const QString &mimeType : preferred) {
        for (const QString &offered : mimeTypes) {
            if (offered.compare(mimeType, Qt::CaseInsensitive) == 0) {
                return offered;
            }
        }
    }
    for (const QString &mimeType : mimeTypes) {
        if (mimeType.startsWith(QStringLiteral("text/"), Qt::CaseInsensitive)) {
            return mimeType;
        }
    }
    return mimeTypes.isEmpty() ? QString() : mimeTypes.first();
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

bool startClipboardOwner(const QList<ClipboardMimePart> &parts, QString *error)
{
    static const bool cleanupRegistered = [] {
        qAddPostRoutine(stopActiveClipboardOwner);
        return true;
    }();
    Q_UNUSED(cleanupRegistered);

    const QString executable = clipboardHelperExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Speecher's Wayland clipboard helper is unavailable");
        }
        return false;
    }

    auto *process = new QProcess;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start(executable);
    if (!process->waitForStarted(clipboardProcessStartTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("Could not start Speecher's Wayland clipboard helper");
        }
        delete process;
        return false;
    }

    process->write(ownerPayload(parts));
    process->closeWriteChannel();
    if (!process->waitForReadyRead(clipboardOwnerReadyTimeoutMs)
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

bool copyBytes(const QByteArray &data, const QString &mimeType, QString *error)
{
    const QString executable = wlCopyExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("wl-copy is not installed");
        }
        return false;
    }

    QStringList arguments;
    if (!mimeType.isEmpty()) {
        arguments << QStringLiteral("--type") << mimeType;
    }
    return runClipboardProcess(executable, QStringLiteral("wl-copy"), arguments, &data, nullptr, error);
}

} // namespace

WlClipboardDelivery::WlClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool WlClipboardDelivery::isAvailable()
{
    return !clipboardHelperExecutable().isEmpty() || !wlCopyExecutable().isEmpty();
}

bool WlClipboardDelivery::isWaylandSession()
{
    if (qGuiApp) {
        return QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                        Qt::CaseInsensitive);
    }
    return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

bool WlClipboardDelivery::canSnapshot()
{
    if (isWaylandSession()) {
        return !wlPasteExecutable().isEmpty();
    }
    return qApp && QApplication::clipboard();
}

bool WlClipboardDelivery::copy(const QString &text, QString *error)
{
    return copy(DeliveryContent{text, std::nullopt}, nullptr, error);
}

bool WlClipboardDelivery::copy(const DeliveryContent &content, bool *htmlAvailable,
                               QString *error)
{
    if (htmlAvailable) {
        *htmlAvailable = false;
    }

    QList<ClipboardMimePart> parts{
        {QStringLiteral("text/plain;charset=utf-8"), content.plainText.toUtf8()},
        {QStringLiteral("text/plain"), content.plainText.toUtf8()},
        {QStringLiteral("UTF8_STRING"), content.plainText.toUtf8()},
    };
    if (content.html) {
        parts.append({QStringLiteral("text/html"), content.html->toUtf8()});
    }

    if (startClipboardOwner(parts, error)) {
        if (htmlAvailable) {
            *htmlAvailable = content.html.has_value();
        }
        return true;
    }
    QString plainError;
    if (copyBytes(content.plainText.toUtf8(), QStringLiteral("text/plain"), &plainError)) {
        return true;
    }
    if (error && error->isEmpty()) {
        *error = plainError;
    }
    return false;
}

bool WlClipboardDelivery::capture(WlClipboardSnapshot *snapshot, QString *error)
{
    if (!snapshot) {
        if (error) {
            *error = QStringLiteral("No clipboard snapshot destination");
        }
        return false;
    }
    *snapshot = {};

    if (!isWaylandSession() && qApp && QApplication::clipboard()) {
        const QMimeData *mime = QApplication::clipboard()->mimeData(QClipboard::Clipboard);
        if (mime) {
            for (const QString &format : mime->formats()) {
                const QByteArray data = mime->data(format);
                if (!data.isEmpty()) {
                    snapshot->parts.append({format, data});
                }
            }
            if (!snapshot->parts.isEmpty()) {
                snapshot->hasData = true;
                snapshot->mimeType = snapshot->parts.first().mimeType;
                snapshot->data = snapshot->parts.first().data;
            }
            return true;
        }
    }

    const QString executable = wlPasteExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("wl-paste is not installed");
        }
        return false;
    }

    QByteArray typeOutput;
    QString typeError;
    if (!runClipboardProcess(executable,
                             QStringLiteral("wl-paste"),
                             {QStringLiteral("--list-types")},
                             nullptr,
                             &typeOutput,
                             &typeError)) {
        if (looksLikeEmptyClipboardError(typeError)) {
            return true;
        }
        if (error) {
            *error = typeError;
        }
        return false;
    }

    QStringList mimeTypes;
    for (const QString &line : QString::fromUtf8(typeOutput).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString mimeType = line.trimmed();
        if (!mimeType.isEmpty()) {
            mimeTypes << mimeType;
        }
    }

    if (mimeTypes.isEmpty()) {
        return true;
    }

    for (const QString &mimeType : mimeTypes) {
        QByteArray data;
        if (!runClipboardProcess(
                executable,
                QStringLiteral("wl-paste"),
                {QStringLiteral("--no-newline"), QStringLiteral("--type"), mimeType},
                nullptr,
                &data,
                error)) {
            return false;
        }
        snapshot->parts.append({mimeType, data});
    }

    snapshot->hasData = true;
    snapshot->mimeType = preferredMimeType(mimeTypes);
    for (const ClipboardMimePart &part : std::as_const(snapshot->parts)) {
        if (part.mimeType == snapshot->mimeType) {
            snapshot->data = part.data;
            break;
        }
    }
    return true;
}

bool WlClipboardDelivery::restore(const WlClipboardSnapshot &snapshot, QString *error)
{
    if (!isWaylandSession() && qApp && QApplication::clipboard()) {
        if (!snapshot.hasData) {
            QApplication::clipboard()->clear(QClipboard::Clipboard);
            return true;
        }
        auto *mime = new QMimeData;
        const QList<ClipboardMimePart> parts = snapshot.parts.isEmpty()
            ? QList<ClipboardMimePart>{{snapshot.mimeType, snapshot.data}}
            : snapshot.parts;
        for (const ClipboardMimePart &part : parts) {
            mime->setData(
                part.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                        : part.mimeType,
                part.data);
        }
        QApplication::clipboard()->setMimeData(mime, QClipboard::Clipboard);
        return true;
    }

    if (!snapshot.hasData) {
        if (!clipboardHelperExecutable().isEmpty()) {
            return startClipboardOwner({}, error);
        }
        const QString executable = wlCopyExecutable();
        if (executable.isEmpty()) {
            if (error) {
                *error = QStringLiteral("wl-copy is not installed");
            }
            return false;
        }
        return runClipboardProcess(executable,
                                   QStringLiteral("wl-copy"),
                                   {QStringLiteral("--clear")},
                                   nullptr,
                                   nullptr,
                                   error);
    }

    const QList<ClipboardMimePart> parts = snapshot.parts.isEmpty()
        ? QList<ClipboardMimePart>{{
              snapshot.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                          : snapshot.mimeType,
              snapshot.data,
          }}
        : snapshot.parts;
    if (!clipboardHelperExecutable().isEmpty()) {
        return startClipboardOwner(parts, error);
    }
    if (parts.size() == 1) {
        return copyBytes(parts.first().data, parts.first().mimeType, error);
    }
    if (error) {
        *error = QStringLiteral("Multi-format clipboard restoration needs Speecher's Wayland clipboard helper");
    }
    return false;
}

} // namespace speecher
