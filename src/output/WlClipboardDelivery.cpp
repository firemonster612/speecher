#include "output/WlClipboardDelivery.h"

#include <QProcess>
#include <QStandardPaths>

namespace speecher {

namespace {

constexpr int clipboardProcessStartTimeoutMs = 1000;
constexpr int clipboardProcessTimeoutMs = 5000;

QString wlCopyExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
}

QString wlPasteExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
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
    return !wlCopyExecutable().isEmpty();
}

bool WlClipboardDelivery::canSnapshot()
{
    return !wlCopyExecutable().isEmpty() && !wlPasteExecutable().isEmpty();
}

bool WlClipboardDelivery::copy(const QString &text, QString *error)
{
    return copyBytes(text.toUtf8(), QStringLiteral("text/plain"), error);
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

    const QString mimeType = preferredMimeType(mimeTypes);
    if (mimeType.isEmpty()) {
        return true;
    }

    QByteArray data;
    if (!runClipboardProcess(executable,
                             QStringLiteral("wl-paste"),
                             {QStringLiteral("--no-newline"), QStringLiteral("--type"), mimeType},
                             nullptr,
                             &data,
                             error)) {
        return false;
    }

    snapshot->hasData = true;
    snapshot->mimeType = mimeType;
    snapshot->data = data;
    return true;
}

bool WlClipboardDelivery::restore(const WlClipboardSnapshot &snapshot, QString *error)
{
    if (!snapshot.hasData) {
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

    const QString mimeType = snapshot.mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : snapshot.mimeType;
    return copyBytes(snapshot.data, mimeType, error);
}

} // namespace speecher
