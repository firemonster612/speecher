#include "output/WlClipboardDelivery.h"

#include "dictation/DictationPorts.h"
#include "output/WaylandClipboardOwner.h"
#include "output/WaylandClipboardProcess.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QStringList>
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

// Copies one format through wl-copy, for when the helper is missing or failed.
void copyBytes(const QByteArray &data,
               const QString &mimeType,
               QObject *context,
               std::function<void(bool, const QString &)> done)
{
    const QString executable = WaylandClipboardProcess::wlCopyExecutable();
    if (executable.isEmpty()) {
        done(false, QStringLiteral("wl-copy is not installed"));
        return;
    }

    QStringList arguments;
    if (!mimeType.isEmpty()) {
        arguments << QStringLiteral("--type") << mimeType;
    }
    WaylandClipboardProcess::run({executable, QStringLiteral("wl-copy"), arguments, data}, context,
                                 [done](const WaylandClipboardProcess::CommandResult &result) {
                                     done(result.ok, result.error);
                                 });
}

// Reads the remaining formats one wl-paste at a time, all within the
// snapshot's deadline, then answers with the whole snapshot.
void captureParts(const QString &executable,
                  QStringList remaining,
                  ClipboardSnapshot snapshot,
                  QElapsedTimer deadline,
                  QObject *context,
                  std::function<void(const ClipboardCaptured &)> done)
{
    if (remaining.isEmpty()) {
        QStringList mimeTypes;
        for (const ClipboardMimePart &part : std::as_const(snapshot.parts)) {
            mimeTypes << part.mimeType;
        }
        snapshot.hasData = true;
        snapshot.mimeType = preferredMimeType(mimeTypes);
        snapshot.data = partData(snapshot.parts, snapshot.mimeType);
        done({true, snapshot, {}});
        return;
    }
    const int remainingMs = snapshotDeadlineMs - int(deadline.elapsed());
    if (remainingMs <= 0) {
        done({false, {}, QStringLiteral("Clipboard snapshot timed out")});
        return;
    }
    const QString mimeType = remaining.takeFirst();
    WaylandClipboardProcess::run(
        {executable, QStringLiteral("wl-paste"), {QStringLiteral("--no-newline"), QStringLiteral("--type"), mimeType},
         std::nullopt, true, remainingMs, maximumFormatBytes},
        context,
        [=](const WaylandClipboardProcess::CommandResult &result) mutable {
            if (!result.ok) {
                done({false, {}, result.error});
                return;
            }
            snapshot.parts.append({mimeType, result.output});
            qsizetype capturedBytes = 0;
            for (const ClipboardMimePart &part : std::as_const(snapshot.parts)) {
                capturedBytes += part.data.size();
            }
            if (capturedBytes > maximumSnapshotBytes) {
                done({false, {}, QStringLiteral("Clipboard snapshot is too large to restore safely")});
                return;
            }
            captureParts(executable, remaining, snapshot, deadline, context, done);
        });
}

} // namespace

WlClipboardDelivery::WlClipboardDelivery(QObject *parent)
    : QObject(parent)
{
}

bool WlClipboardDelivery::isAvailable()
{
    return !WaylandClipboardProcess::helperExecutable().isEmpty()
        || !WaylandClipboardProcess::wlCopyExecutable().isEmpty();
}

bool WlClipboardDelivery::isWaylandSession()
{
    // A QCoreApplication has no platform to ask, so it goes by the
    // environment, as no application at all does.
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                        Qt::CaseInsensitive);
    }
    return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

bool WlClipboardDelivery::canSnapshot()
{
    return !WaylandClipboardProcess::wlPasteExecutable().isEmpty();
}

void WlClipboardDelivery::copy(const DeliveryContent &content,
                               QObject *context,
                               std::function<void(const ClipboardCopied &)> done)
{
    const QByteArray text = content.plainText.toUtf8();
    QList<ClipboardMimePart> parts{
        {QStringLiteral("text/plain;charset=utf-8"), text},
        {QStringLiteral("text/plain"), text},
        {QStringLiteral("UTF8_STRING"), text},
    };
    if (content.html) {
        parts.append({QStringLiteral("text/html"), content.html->toUtf8()});
    }
    parts.append({copyMarkerMimeType(), QUuid::createUuid().toByteArray()});
    m_copiedParts.clear();

    // The helper's failure is the one reported when wl-copy fails too.
    const auto copyPlainText = [this, context, done, text](const QString &helperError) {
        copyBytes(text, QStringLiteral("text/plain"), context, [this, done, text, helperError](bool ok, const QString &error) {
            if (ok) {
                m_copiedParts = {{QStringLiteral("text/plain"), text}};
            }
            done({ok, false, ok ? QString() : helperError.isEmpty() ? error : helperError});
        });
    };
    const QString helper = WaylandClipboardProcess::helperExecutable();
    if (helper.isEmpty()) {
        copyPlainText(QStringLiteral("Speecher's Wayland clipboard helper is unavailable"));
        return;
    }
    WaylandClipboardOwner::start(helper, parts, context,
                                 [this, done, parts, copyPlainText, html = content.html.has_value()](
                                     bool published, const QString &error) {
                                     if (!published) {
                                         copyPlainText(error);
                                         return;
                                     }
                                     m_copiedParts = parts;
                                     done({true, html, {}});
                                 });
}

void WlClipboardDelivery::capture(QObject *context, std::function<void(const ClipboardCaptured &)> done)
{
    const QString executable = WaylandClipboardProcess::wlPasteExecutable();
    if (executable.isEmpty()) {
        done({false, {}, QStringLiteral("wl-paste is not installed")});
        return;
    }

    QElapsedTimer deadline;
    deadline.start();
    WaylandClipboardProcess::run(
        {executable, QStringLiteral("wl-paste"), {QStringLiteral("--list-types")}, std::nullopt, true,
         snapshotDeadlineMs, 64 * 1024},
        context,
        [executable, deadline, context, done](const WaylandClipboardProcess::CommandResult &types) {
            if (!types.ok) {
                if (WaylandClipboardProcess::looksLikeEmptyClipboardError(types.error)) {
                    done({true, {}, {}});
                } else {
                    done({false, {}, types.error});
                }
                return;
            }
            // Every offered format, private ones too: restoring less than the
            // owner offered would drop images or app-private data.
            QStringList mimeTypes;
            for (const QString &line : QString::fromUtf8(types.output).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString mimeType = line.trimmed();
                if (!mimeType.isEmpty()) {
                    mimeTypes << mimeType;
                }
            }
            if (mimeTypes.isEmpty()) {
                done({true, {}, {}});
                return;
            }
            captureParts(executable, mimeTypes, {}, deadline, context, done);
        });
}

// Clipboard managers re-offer our copy under their own format set, and the
// wl-copy fallback offers text/plain alone, so comparing the whole set made
// restore a silent no-op. Our dictation is still the selection if its marker
// survived; a different marker can only come from another Speecher copy and is
// definitive proof the selection moved on. With the marker absent the plain
// text still decides (deliberate: managers like Klipper re-offer our copy
// without the marker, and dropping this fallback regresses restore under
// them). Accepted residual: a user copy of the identical plain text with
// different formatting inside the verification window is indistinguishable
// from a manager re-offer and gets restored over. See RESIDUALS.md.
bool WlClipboardDelivery::copyStillOnClipboard(const QList<ClipboardMimePart> &copied,
                                               const QList<ClipboardMimePart> &current)
{
    const QByteArray marker = partData(copied, copyMarkerMimeType());
    const QByteArray currentMarker = partData(current, copyMarkerMimeType());
    if (!marker.isEmpty() && currentMarker == marker) {
        return true;
    }
    if (!currentMarker.isEmpty()) {
        return false;
    }
    const QByteArray copiedText = plainTextData(copied);
    return !copiedText.isEmpty() && plainTextData(current) == copiedText;
}

void WlClipboardDelivery::restorePreservingNewCopy(const ClipboardSnapshot &snapshot,
                                                   QObject *context,
                                                   std::function<void(const ClipboardRestored &)> done)
{
    if (m_copiedParts.isEmpty()) {
        done({true, false, {}});
        return;
    }
    capture(context, [this, snapshot, context, done](const ClipboardCaptured &current) {
        if (!current.ok) {
            done({false, false, current.error});
            return;
        }
        // The decision has been made for this copy. Keeping the parts would
        // let a later delivery that never copied anything restore over
        // somebody else's clipboard on the strength of this one.
        const QList<ClipboardMimePart> copied = std::exchange(m_copiedParts, {});
        if (!copyStillOnClipboard(copied, current.snapshot.parts)) {
            done({true, true, {}});
            return;
        }
        restore(snapshot, context, done);
    });
}

void WlClipboardDelivery::restore(const ClipboardSnapshot &snapshot,
                                  QObject *context,
                                  std::function<void(const ClipboardRestored &)> done)
{
    const auto answer = [done](bool ok, const QString &error) { done({ok, false, ok ? QString() : error}); };
    const QString helper = WaylandClipboardProcess::helperExecutable();
    if (!snapshot.hasData) {
        if (!helper.isEmpty()) {
            WaylandClipboardOwner::start(helper, {}, context, answer);
            return;
        }
        const QString executable = WaylandClipboardProcess::wlCopyExecutable();
        if (executable.isEmpty()) {
            answer(false, QStringLiteral("wl-copy is not installed"));
            return;
        }
        WaylandClipboardProcess::run({executable, QStringLiteral("wl-copy"), {QStringLiteral("--clear")}}, context,
                                     [answer](const WaylandClipboardProcess::CommandResult &result) {
                                         answer(result.ok, result.error);
                                     });
        return;
    }

    const QList<ClipboardMimePart> parts = snapshot.parts.isEmpty()
        ? QList<ClipboardMimePart>{{
              snapshot.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                          : snapshot.mimeType,
              snapshot.data,
          }}
        : snapshot.parts;
    if (!helper.isEmpty()) {
        WaylandClipboardOwner::start(helper, parts, context, answer);
        return;
    }
    if (parts.size() == 1) {
        copyBytes(parts.first().data, parts.first().mimeType, context, answer);
        return;
    }
    answer(false, QStringLiteral("Multi-format clipboard restoration needs Speecher's Wayland clipboard helper"));
}

} // namespace speecher
