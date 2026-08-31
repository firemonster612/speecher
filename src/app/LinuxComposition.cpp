#include "app/LinuxComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
#include "output/TextDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "platform/AtSpiTargetProvider.h"
#include "platform/KGlobalAccelShortcutBinder.h"
#include "platform/MediaPauseController.h"
#include "platform/PortalScreenshotContextProvider.h"
#include "platform/WaylandLayerShell.h"
#include "platform/atspi/AtSpiAccess.h"
#include "platform/audio/QtAudioInput.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace speecher {
namespace {

QString appImageSocketName()
{
    return appSocketName(QStringLiteral("appimage"));
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

} // namespace

QString LinuxComposition::outputSummary() const
{
    return QStringLiteral("Automatic: Wayland multi-format clipboard, optional ydotool paste, Qt fallback");
}

QString LinuxComposition::primaryOutputStatus() const
{
    return WlClipboardDelivery::isAvailable()
        ? QStringLiteral("Wayland multi-format clipboard available")
        : QStringLiteral("Qt clipboard fallback");
}

QString LinuxComposition::ipcListenName() const
{
    return isRunningFromOwnAppImage() ? appImageSocketName() : appSocketName();
}

QStringList LinuxComposition::ipcConnectCandidates() const
{
    if (isRunningFromOwnAppImage()) {
        return {appImageSocketName()};
    }
    return {appSocketName(), executablePathSocketName()};
}

QString LinuxComposition::detachedExecutablePath() const
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

QList<AudioInputDeviceInfo> LinuxComposition::availableAudioInputDevices() const
{
    return QtAudioInput::availableInputDevices();
}

AudioInput *LinuxComposition::createAudioInput(SettingsStore *settings, QObject *parent) const
{
    auto *input = new QtAudioInput(settings->audioCaptureSettings(), parent);
    QObject::connect(settings,
                     &SettingsStore::audioCaptureSettingsChanged,
                     input,
                     &QtAudioInput::applySettings);
    return input;
}

MediaController *LinuxComposition::createMediaController(QObject *parent) const
{
    return new MediaPauseController(parent);
}

TargetProvider *LinuxComposition::createTargetProvider(QObject *parent) const
{
    return new AtSpiTargetProvider(parent);
}

ScreenshotContextProvider *LinuxComposition::createScreenshotContextProvider(QObject *parent) const
{
    return new PortalScreenshotContextProvider(parent);
}

TextDeliveryAdapter *LinuxComposition::createTextDelivery(TargetProvider *targetProvider, QObject *parent) const
{
    return new TextDelivery(targetProvider, parent);
}

PopupPositioner *LinuxComposition::createPopupPositioner(QObject *parent) const
{
    return new WaylandLayerShell(parent);
}

GlobalShortcutBinder *LinuxComposition::createGlobalShortcutBinder(QObject *parent) const
{
    return new KGlobalAccelShortcutBinder(parent);
}

AccessibilityState LinuxComposition::accessibilityState() const
{
    return atspi::accessibilityState();
}

bool LinuxComposition::requestAccessibility(QString *error) const
{
    return atspi::requestAccessibility(error);
}

bool LinuxComposition::enableAccessibilityPermanently(QString *error) const
{
    return atspi::enableAccessibilityPermanently(error);
}

std::shared_ptr<const LinuxComposition> linuxComposition()
{
    return std::make_shared<LinuxComposition>();
}

} // namespace speecher
