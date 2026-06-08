#include "platform/PlatformIntegration.h"

#include "core/AudioCapture.h"
#include "core/MediaPauseController.h"
#include "output/TextDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "ui/WaylandLayerShell.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace speecher {

namespace {

QString userToken()
{
#ifdef Q_OS_UNIX
    return QString::number(getuid());
#else
    const QString user = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("user")));
    return QString::fromLatin1(QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
#endif
}

QString stableAppSocketName()
{
    return QStringLiteral("speecher-%1").arg(userToken());
}

QString appImageSocketName()
{
    return QStringLiteral("speecher-%1-appimage").arg(userToken());
}

bool isRunningFromOwnAppImage()
{
    const QString appDir = QString::fromLocal8Bit(qgetenv("APPDIR"));
    if (qgetenv("APPIMAGE").isEmpty() || appDir.isEmpty()) {
        return false;
    }

    const QString executablePath = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const QString appDirPath = QFileInfo(appDir).canonicalFilePath();
    return !executablePath.isEmpty()
        && !appDirPath.isEmpty()
        && executablePath.startsWith(appDirPath + QDir::separator());
}

QString executablePathSocketName()
{
    const QFileInfo executable(QCoreApplication::applicationFilePath());
    QString path = executable.canonicalFilePath();
    if (path.isEmpty()) {
        path = executable.absoluteFilePath();
    }
    const QByteArray digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("speecher-%1-%2").arg(userToken(), QString::fromLatin1(digest));
}

class LinuxPlatformIntegration final : public PlatformIntegration {
public:
    QString id() const override
    {
        return QStringLiteral("linux");
    }

    QString outputSummary() const override
    {
        return QStringLiteral("Automatic: ydotool paste when enabled, wl-copy, Qt clipboard");
    }

    QString primaryOutputStatus() const override
    {
        return WlClipboardDelivery::isAvailable() ? QStringLiteral("wl-copy available") : QStringLiteral("Qt clipboard fallback");
    }

    QString ipcListenName() const override
    {
        return isRunningFromOwnAppImage() ? appImageSocketName() : stableAppSocketName();
    }

    QStringList ipcConnectCandidates() const override
    {
        if (isRunningFromOwnAppImage()) {
            return {appImageSocketName()};
        }
        return {stableAppSocketName(), executablePathSocketName()};
    }

    QString detachedExecutablePath() const override
    {
        QString program = QCoreApplication::applicationFilePath();
        const QString appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
        const QString appDir = QString::fromLocal8Bit(qgetenv("APPDIR"));
        if (!appImage.isEmpty() && !appDir.isEmpty()) {
            const QString executablePath = QFileInfo(program).canonicalFilePath();
            const QString appDirPath = QFileInfo(appDir).canonicalFilePath();
            if (!executablePath.isEmpty()
                && !appDirPath.isEmpty()
                && executablePath.startsWith(appDirPath + QDir::separator())) {
                program = appImage;
            }
        }
        return program;
    }

    AudioInput *createAudioInput(QObject *parent) const override
    {
        return new AudioCapture(parent);
    }

    MediaController *createMediaController(QObject *parent) const override
    {
        return new MediaPauseController(parent);
    }

    TextDeliveryAdapter *createTextDelivery(QObject *parent) const override
    {
        return new TextDelivery(parent);
    }

    PopupPositioner *createPopupPositioner(QObject *parent) const override
    {
        return new WaylandLayerShell(parent);
    }
};

} // namespace

std::shared_ptr<const PlatformIntegration> PlatformFactory::create()
{
    return std::make_shared<LinuxPlatformIntegration>();
}

} // namespace speecher
