#include "app/LinuxComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
#include "output/TextDelivery.h"
#include "output/WlClipboardDelivery.h"
#include "platform/AtSpiTargetProvider.h"
#include "platform/KGlobalAccelShortcutBinder.h"
#include "platform/KeywatchShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"
#include "platform/MediaPauseController.h"
#include "platform/PortalGlobalShortcutBinder.h"
#include "platform/RoutingShortcutBinder.h"
#include "platform/X11KeyGrabShortcutBinder.h"
#include "platform/XInput2ShortcutBinder.h"
#include "platform/PortalScreenshotContextProvider.h"
#include "platform/WaylandLayerShell.h"
#include "platform/atspi/AtSpiAccess.h"
#ifdef SPEECHER_WITH_PULSE
#include "platform/audio/PulseSystemAudioInput.h"
#endif
#include "platform/audio/QtAudioInput.h"
#include "platform/audio/WavFileAudioInput.h"
#ifdef SPEECHER_WITH_WEBRTC_AEC
#include "platform/audio/WebRtcEchoCanceller.h"
#endif
#include "recording/RecordingPresentation.h"

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
    return QStringLiteral("Automatic: copies to the clipboard, pastes with the virtual keyboard when set up");
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
    const QString wavSeam = qEnvironmentVariable("SPEECHER_AUDIO_WAV");
    if (!wavSeam.isEmpty()) {
        return new WavFileAudioInput(wavSeam, parent);
    }
    auto *input = new QtAudioInput(settings->audioCaptureSettings(), parent);
    QObject::connect(settings,
                     &SettingsStore::audioCaptureSettingsChanged,
                     input,
                     &QtAudioInput::applySettings);
    return input;
}

// Skip silence would throw the echo canceller's alignment off, so it is left
// off however the settings change.
AudioInput *LinuxComposition::createRecordingAudioInput(SettingsStore *settings, QObject *parent) const
{
    const QString wavSeam = qEnvironmentVariable("SPEECHER_AUDIO_WAV");
    if (!wavSeam.isEmpty()) {
        return new WavFileAudioInput(wavSeam, parent);
    }
    const auto whole = [](AudioCaptureSettings capture) {
        capture.vadEnabled = false;
        return capture;
    };
    auto *input = new QtAudioInput(whole(settings->audioCaptureSettings()), parent);
    QObject::connect(settings, &SettingsStore::audioCaptureSettingsChanged, input,
                     [input, whole](const AudioCaptureSettings &capture) { input->applySettings(whole(capture)); });
    return input;
}

bool LinuxComposition::recordsSystemAudio() const
{
#ifdef SPEECHER_WITH_PULSE
    return true;
#else
    return false;
#endif
}

AudioInput *LinuxComposition::createSystemAudioInput(QObject *parent) const
{
#ifdef SPEECHER_WITH_PULSE
    return new PulseSystemAudioInput(parent);
#else
    Q_UNUSED(parent);
    return nullptr;
#endif
}

std::unique_ptr<EchoCanceller> LinuxComposition::createEchoCanceller(QString *warning) const
{
#ifdef SPEECHER_WITH_WEBRTC_AEC
    int error = 0;
    std::unique_ptr<EchoCanceller> canceller = WebRtcEchoCanceller::create(&error);
    if (!canceller && warning) {
        *warning = echoCancellationFailedText(error);
    }
    return canceller;
#else
    if (warning) {
        *warning = echoCancellationNotBuiltText();
    }
    return nullptr;
#endif
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

namespace {

// The desktop shortcut service takes a combination: KGlobalAccel on Plasma,
// otherwise the portal. Under X11 the Cancel and Pause Shortcuts grab their
// keys themselves instead, just while dictating, which the portal cannot.
GlobalShortcutBinder *createCombinationBinder(const GlobalShortcutAction &action)
{
    if (action.sessionOnly) {
        auto *plasma = new KGlobalAccelSessionShortcutBinder(action);
        if (plasma->supported()) {
            return plasma;
        }
        delete plasma;
#ifdef SPEECHER_WITH_X11
        if (!isWaylandSession()) {
            return new X11KeyGrabShortcutBinder(action);
        }
#endif
        return new PortalGlobalShortcutBinder(action);
    }
    auto *plasma = new KGlobalAccelShortcutBinder(action);
    if (plasma->supported()) {
        return plasma;
    }
    delete plasma;
    return new PortalGlobalShortcutBinder(action);
}

// The single-key backend watches the key itself: XInput2 under X11, the
// key-watch helper under Wayland. The session type decides which; a headless
// or unknown session gets the X11 watcher, which reports its own unsupported
// reason when it cannot reach a server.
GlobalShortcutBinder *createSingleKeyBinder(const GlobalShortcutAction &action)
{
#ifdef SPEECHER_WITH_X11
    if (!isWaylandSession()) {
        return new XInput2ShortcutBinder(action);
    }
#endif
    // The key-watch daemon reads evdev, so it works on X11 too; a build
    // without the XInput2 backend (no libxi dev files) falls back to it.
    return new KeywatchShortcutBinder(action);
}

} // namespace

GlobalShortcutBinder *LinuxComposition::createGlobalShortcutBinder(GlobalShortcutRole role,
                                                                   QObject *parent) const
{
    const GlobalShortcutAction action = GlobalShortcutBinder::actionFor(role);
    return new RoutingShortcutBinder(createCombinationBinder(action),
                                     createSingleKeyBinder(action),
                                     parent);
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
