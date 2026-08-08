#include "output/WlClipboardDelivery.h"

#include "dictation/DictationPorts.h"
#include "output/WaylandClipboardOwner.h"
#include "output/WaylandClipboardProcess.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace speecher {

namespace {

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
    const QString executable = WaylandClipboardProcess::wlCopyExecutable();
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
    return WaylandClipboardProcess::run(
        executable, QStringLiteral("wl-copy"), arguments, &data, nullptr, error);
}

} // namespace

WlClipboardDelivery::WlClipboardDelivery(QObject *parent)
    : QObject(parent)
    , m_owner(std::make_unique<WaylandClipboardOwner>())
{
}

WlClipboardDelivery::~WlClipboardDelivery() = default;

bool WlClipboardDelivery::isAvailable()
{
    return !WaylandClipboardProcess::helperExecutable().isEmpty()
        || !WaylandClipboardProcess::wlCopyExecutable().isEmpty();
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
        return !WaylandClipboardProcess::wlPasteExecutable().isEmpty();
    }
    return qApp && QApplication::clipboard();
}

bool WlClipboardDelivery::readText(QString *text, QString *error)
{
    if (!text) {
        if (error) {
            *error = QStringLiteral("No clipboard text destination");
        }
        return false;
    }
    if (!isWaylandSession() && qApp && QApplication::clipboard()) {
        *text = QApplication::clipboard()->text(QClipboard::Clipboard);
        return true;
    }

    const QString executable = WaylandClipboardProcess::wlPasteExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("wl-paste is not installed");
        }
        return false;
    }
    QByteArray output;
    if (!WaylandClipboardProcess::run(
            executable,
            QStringLiteral("wl-paste"),
            {QStringLiteral("--no-newline"), QStringLiteral("--type"), QStringLiteral("text/plain")},
            nullptr,
            &output,
            error)) {
        return false;
    }
    *text = QString::fromUtf8(output);
    return true;
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

    if (m_owner->start(parts, error)) {
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

    const QString executable = WaylandClipboardProcess::wlPasteExecutable();
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral("wl-paste is not installed");
        }
        return false;
    }

    QByteArray typeOutput;
    QString typeError;
    if (!WaylandClipboardProcess::run(executable,
                             QStringLiteral("wl-paste"),
                             {QStringLiteral("--list-types")},
                             nullptr,
                             &typeOutput,
                             &typeError)) {
        if (WaylandClipboardProcess::looksLikeEmptyClipboardError(typeError)) {
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
        if (!WaylandClipboardProcess::run(
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
        if (!WaylandClipboardProcess::helperExecutable().isEmpty()) {
            WaylandClipboardOwner owner;
            return owner.start({}, error);
        }
        const QString executable = WaylandClipboardProcess::wlCopyExecutable();
        if (executable.isEmpty()) {
            if (error) {
                *error = QStringLiteral("wl-copy is not installed");
            }
            return false;
        }
        return WaylandClipboardProcess::run(executable,
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
    if (!WaylandClipboardProcess::helperExecutable().isEmpty()) {
        WaylandClipboardOwner owner;
        return owner.start(parts, error);
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
