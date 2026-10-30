#include "app/WindowsComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
#include "output/TextDelivery.h"
#include "platform/FallbackPopupPositioner.h"
#include "platform/audio/E2EAudioInput.h"
#include "platform/audio/QtAudioInput.h"
#include "platform/RoutingShortcutBinder.h"
#include "platform/win/WinSessionKeyGrab.h"
#include "platform/win/WinCommunicationsAudioInput.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "platform/win/WinMediaController.h"
#include "platform/win/WinScreenshotContextProvider.h"
#include "platform/win/WinSingleKeyShortcutBinder.h"
#include "platform/win/WinSystemAudioInput.h"
#include "platform/win/WinTargetProvider.h"
#include "platform/win/WinWindowBackdrop.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QUrl>

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>

namespace speecher {
namespace {

constexpr auto runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto runValue = "Speecher";
// Settings > Privacy & security > Microphone keeps its switches here, under
// HKEY_LOCAL_MACHINE for the device and HKEY_CURRENT_USER for this person:
// Value is Allow or Deny, and NonPackaged's is desktop apps'.
constexpr auto microphoneConsent =
    "\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone";

bool consentDenied(const char *root, const char *key)
{
    const QSettings consent(QString::fromLatin1(root) + QString::fromLatin1(microphoneConsent), QSettings::NativeFormat);
    return consent.value(QString::fromLatin1(key)).toString() == QLatin1String("Deny");
}

// The stub end-to-end tests stand in for the microphone, or none.
AudioInput *stubAudioInput(QObject *parent)
{
#ifdef SPEECHER_E2E_HOOKS
    if (qEnvironmentVariableIntValue("SPEECHER_E2E_STUB") == 1
        && qEnvironmentVariableIntValue("SPEECHER_E2E_REAL_AUDIO") != 1) {
        return new E2EAudioInput(parent);
    }
#endif
    Q_UNUSED(parent);
    return nullptr;
}

QString launchCommand()
{
    return QStringLiteral("\"%1\" --daemon")
        .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
}

} // namespace

QString WindowsComposition::outputSummary() const
{
    return QStringLiteral("Automatic: keyboard paste (Ctrl+V), Qt clipboard fallback");
}

QString WindowsComposition::ipcListenName() const
{
    return appSocketName();
}

QStringList WindowsComposition::ipcConnectCandidates() const
{
    return {appSocketName(), executablePathSocketName()};
}

QString WindowsComposition::detachedExecutablePath() const
{
    return QCoreApplication::applicationFilePath();
}

QList<AudioInputDeviceInfo> WindowsComposition::availableAudioInputDevices() const
{
    return QtAudioInput::availableInputDevices();
}

AudioInput *WindowsComposition::createAudioInput(SettingsStore *settings, QObject *parent) const
{
    if (AudioInput *stub = stubAudioInput(parent)) {
        return stub;
    }
    auto *input = new QtAudioInput(settings->dictationSnapshot().audio, parent);
    QObject::connect(settings,
                     &SettingsStore::audioCaptureSettingsChanged,
                     input,
                     &QtAudioInput::applySettings);
    return input;
}

AudioInput *WindowsComposition::createRecordingAudioInput(SettingsStore *settings, QObject *parent) const
{
    if (AudioInput *stub = stubAudioInput(parent)) {
        return stub;
    }
    return new WinCommunicationsAudioInput(settings->dictationSnapshot().audio, streamEchoControl, parent);
}

// WASAPI loopback captures system audio, and Windows cancels its echo in the
// recording's communications microphone, so there is no canceller of
// Speecher's.
bool WindowsComposition::recordsSystemAudio() const
{
    return true;
}

AudioInput *WindowsComposition::createSystemAudioInput(QObject *parent) const
{
    return new WinSystemAudioInput(parent);
}

MicrophoneAccess WindowsComposition::microphoneAccess() const
{
    if (consentDenied("HKEY_LOCAL_MACHINE", "Value")) {
        return MicrophoneAccess::DeviceOff;
    }
    if (consentDenied("HKEY_CURRENT_USER", "Value")) {
        return MicrophoneAccess::AppsOff;
    }
    if (consentDenied("HKEY_CURRENT_USER", "NonPackaged/Value")) {
        return MicrophoneAccess::DesktopAppsOff;
    }
    return MicrophoneAccess::Allowed;
}

void WindowsComposition::openMicrophoneSettings() const
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:privacy-microphone")));
}

MediaController *WindowsComposition::createMediaController(QObject *parent) const
{
    return new WinMediaController(parent);
}

TargetProvider *WindowsComposition::createTargetProvider(QObject *parent) const
{
    return new WinTargetProvider(parent);
}

ScreenshotContextProvider *WindowsComposition::createScreenshotContextProvider(QObject *parent) const
{
    return new WinScreenshotContextProvider(parent);
}

TextDeliveryAdapter *WindowsComposition::createTextDelivery(TargetProvider *targetProvider, QObject *parent) const
{
    return new TextDelivery(targetProvider, parent);
}

PopupPositioner *WindowsComposition::createPopupPositioner(QObject *parent) const
{
    return new FallbackPopupPositioner(parent);
}

WindowBackdrop *WindowsComposition::createWindowBackdrop(QObject *parent) const
{
    return new WinWindowBackdrop(parent);
}

GlobalShortcutBinder *WindowsComposition::createGlobalShortcutBinder(GlobalShortcutRole role,
                                                                     SettingsStore *settings,
                                                                     QObject *parent) const
{
    const GlobalShortcutAction action = GlobalShortcutBinder::actionFor(role);
    GlobalShortcutBinder *combination = action.sessionOnly
        ? static_cast<GlobalShortcutBinder *>(new WinSessionShortcutBinder(settings, action))
        : new WinGlobalShortcutBinder(settings, action);
    return new RoutingShortcutBinder(combination, new WinSingleKeyShortcutBinder(settings, action), parent);
}

SessionKeyGrab *WindowsComposition::createSessionKeyGrab(QObject *parent) const
{
    return new WinSessionKeyGrab(parent);
}

AccessibilityState WindowsComposition::accessibilityState() const
{
    return {true, true, true};
}

bool WindowsComposition::requestAccessibility(QString *error) const
{
    Q_UNUSED(error);
    return true;
}

bool WindowsComposition::enableAccessibilityPermanently(QString *error) const
{
    Q_UNUSED(error);
    return true;
}

bool WindowsComposition::setLaunchAtLogin(bool enabled, QString *error) const
{
    QSettings settings(QString::fromLatin1(runKey), QSettings::NativeFormat);
    if (enabled) {
        settings.setValue(QString::fromLatin1(runValue), launchCommand());
    } else {
        settings.remove(QString::fromLatin1(runValue));
    }
    settings.sync();
    if (settings.status() == QSettings::NoError) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Windows could not update the Startup Apps entry");
    }
    return false;
}

bool WindowsComposition::launchAtLoginEnabled() const
{
    QSettings settings(QString::fromLatin1(runKey), QSettings::NativeFormat);
    return settings.value(QString::fromLatin1(runValue)).toString() == launchCommand();
}

std::optional<float> WindowsComposition::inputVolume() const
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> volume;
    float level = 0.0f;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&enumerator)))
        || FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))
        || FAILED(device->Activate(__uuidof(IAudioEndpointVolume),
                                   CLSCTX_INPROC_SERVER,
                                   nullptr,
                                   reinterpret_cast<void **>(volume.GetAddressOf())))
        || FAILED(volume->GetMasterVolumeLevelScalar(&level))) {
        return std::nullopt;
    }
    return std::clamp(level, 0.0f, 1.0f);
}

std::shared_ptr<const WindowsComposition> windowsComposition()
{
    return std::make_shared<WindowsComposition>();
}

} // namespace speecher
