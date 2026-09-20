#include "output/WlClipboardDelivery.h"

#include "dictation/DictationPorts.h"
#include "output/WaylandClipboardOwner.h"
#include "output/WaylandClipboardProcess.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QUuid>

#include <utility>

namespace speecher {

namespace {

constexpr int snapshotDeadlineMs = 1500;
constexpr qsizetype maximumFormatBytes = 4 * 1024 * 1024;
constexpr qsizetype maximumSnapshotBytes = 8 * 1024 * 1024;

// A private format carrying a per-copy UUID. When the offer survives intact we
// know the clipboard still holds our dictation and not someone else's newer copy.
QString copyMarkerMimeType()
{
    return QStringLiteral("application/x-speecher-copy-id");
}

QByteArray partData(const QList<ClipboardMimePart> &parts, const QString &mimeType)
{
    for (const ClipboardMimePart &part : parts) {
        if (part.mimeType.compare(mimeType, Qt::CaseInsensitive) == 0) {
            return part.data;
        }
    }
    return {};
}

QByteArray plainTextData(const QList<ClipboardMimePart> &parts)
{
    const QByteArray utf8 = partData(parts, QStringLiteral("text/plain;charset=utf-8"));
    return utf8.isEmpty() ? partData(parts, QStringLiteral("text/plain")) : utf8;
}

const QStringList &restorableMimeTypes()
{
    static const QStringList mimeTypes{
        QStringLiteral("text/plain;charset=utf-8"),
        QStringLiteral("text/plain"),
        QStringLiteral("UTF8_STRING"),
        QStringLiteral("text/html"),
        QStringLiteral("text/uri-list"),
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/jpg"),
    };
    return mimeTypes;
}

QString preferredMimeType(const QStringList &mimeTypes)
{
    for (const QString &mimeType : restorableMimeTypes()) {
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
    return qApp && QGuiApplication::clipboard();
}

bool WlClipboardDelivery::readText(QString *text, QString *error)
{
    if (!text) {
        if (error) {
            *error = QStringLiteral("No clipboard text destination");
        }
        return false;
    }
    if (!isWaylandSession() && qApp && QGuiApplication::clipboard()) {
        *text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
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

QStringList WlClipboardDelivery::snapshotMimeTypes(const QStringList &offeredMimeTypes)
{
    return offeredMimeTypes;
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

    m_copiedParts.clear();
    parts.append({copyMarkerMimeType(), QUuid::createUuid().toByteArray()});
    if (m_owner->start(parts, error)) {
        m_copiedParts = parts;
        if (htmlAvailable) {
            *htmlAvailable = content.html.has_value();
        }
        return true;
    }
    QString plainError;
    if (copyBytes(content.plainText.toUtf8(), QStringLiteral("text/plain"), &plainError)) {
        m_copiedParts = {{QStringLiteral("text/plain"), content.plainText.toUtf8()}};
        return true;
    }
    if (error && error->isEmpty()) {
        *error = plainError;
    }
    return false;
}

bool WlClipboardDelivery::capture(ClipboardSnapshot *snapshot, QString *error)
{
    if (!snapshot) {
        if (error) {
            *error = QStringLiteral("No clipboard snapshot destination");
        }
        return false;
    }
    *snapshot = {};

    if (!isWaylandSession() && qApp && QGuiApplication::clipboard()) {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData(QClipboard::Clipboard);
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

    QElapsedTimer deadline;
    deadline.start();
    QByteArray typeOutput;
    QString typeError;
    if (!WaylandClipboardProcess::run(executable,
                             QStringLiteral("wl-paste"),
                             {QStringLiteral("--list-types")},
                             nullptr,
                             &typeOutput,
                             &typeError,
                             snapshotDeadlineMs,
                             64 * 1024)) {
        if (WaylandClipboardProcess::looksLikeEmptyClipboardError(typeError)) {
            return true;
        }
        if (error) {
            *error = typeError;
        }
        return false;
    }

    QStringList offeredMimeTypes;
    for (const QString &line : QString::fromUtf8(typeOutput).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString mimeType = line.trimmed();
        if (!mimeType.isEmpty()) {
            offeredMimeTypes << mimeType;
        }
    }
    const QStringList mimeTypes = snapshotMimeTypes(offeredMimeTypes);

    if (offeredMimeTypes.isEmpty()) {
        return true;
    }
    if (mimeTypes.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The current clipboard has no restorable formats");
        }
        return false;
    }

    qsizetype capturedBytes = 0;
    for (const QString &mimeType : mimeTypes) {
        const int remainingMs = snapshotDeadlineMs - int(deadline.elapsed());
        if (remainingMs <= 0) {
            if (error) {
                *error = QStringLiteral("Clipboard snapshot timed out");
            }
            *snapshot = {};
            return false;
        }
        QByteArray data;
        if (!WaylandClipboardProcess::run(
                executable,
                QStringLiteral("wl-paste"),
                {QStringLiteral("--no-newline"), QStringLiteral("--type"), mimeType},
                nullptr,
                &data,
                error,
                remainingMs,
                maximumFormatBytes)) {
            *snapshot = {};
            return false;
        }
        capturedBytes += data.size();
        if (capturedBytes > maximumSnapshotBytes) {
            if (error) {
                *error = QStringLiteral("Clipboard snapshot is too large to restore safely");
            }
            *snapshot = {};
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

// Clipboard managers re-offer our copy under their own format set, and the
// wl-copy fallback offers text/plain alone, so comparing the whole set made
// restore a silent no-op. Our dictation is still the selection if its marker
// survived or the plain text still matches; anything else is somebody's newer
// copy, which we must never overwrite.
bool WlClipboardDelivery::copyStillOnClipboard(const QList<ClipboardMimePart> &copied,
                                               const QList<ClipboardMimePart> &current)
{
    const QByteArray marker = partData(copied, copyMarkerMimeType());
    if (!marker.isEmpty() && partData(current, copyMarkerMimeType()) == marker) {
        return true;
    }
    const QByteArray copiedText = plainTextData(copied);
    return !copiedText.isEmpty() && plainTextData(current) == copiedText;
}

bool WlClipboardDelivery::restorePreservingNewCopy(const ClipboardSnapshot &snapshot,
                                                   QString *error,
                                                   bool *keptNewerCopy)
{
    if (keptNewerCopy) {
        *keptNewerCopy = false;
    }
    if (m_copiedParts.isEmpty()) {
        return true;
    }
    ClipboardSnapshot current;
    if (!capture(&current, error)) {
        return false;
    }
    // The decision has been made for this copy. Keeping the parts would let a
    // later delivery that never copied anything restore over somebody else's
    // clipboard on the strength of this one.
    const QList<ClipboardMimePart> copied = std::exchange(m_copiedParts, {});
    if (!copyStillOnClipboard(copied, current.parts)) {
        if (keptNewerCopy) {
            *keptNewerCopy = true;
        }
        return true;
    }
    return restore(snapshot, error);
}

bool WlClipboardDelivery::restore(const ClipboardSnapshot &snapshot, QString *error)
{
    if (!isWaylandSession() && qApp && QGuiApplication::clipboard()) {
        if (!snapshot.hasData) {
            QGuiApplication::clipboard()->clear(QClipboard::Clipboard);
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
        QGuiApplication::clipboard()->setMimeData(mime, QClipboard::Clipboard);
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
