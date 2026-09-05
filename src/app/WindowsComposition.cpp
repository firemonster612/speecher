#include "app/WindowsComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
#include "output/TextDelivery.h"
#include "platform/FallbackPopupPositioner.h"
#include "platform/audio/QtAudioInput.h"
#include "platform/win/WinGlobalShortcutBinder.h"
#include "platform/win/WinMediaController.h"
#include "platform/win/WinScreenshotContextProvider.h"
#include "platform/win/WinTargetProvider.h"

#include <QCoreApplication>
#include <QSettings>

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>

namespace speecher {
namespace {

constexpr auto runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto runValue = "Speecher";

QString launchCommand()
{
    return QStringLiteral("\"%1\" --daemon").arg(QCoreApplication::applicationFilePath());
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
    auto *input = new QtAudioInput(settings->audioCaptureSettings(), parent);
    QObject::connect(settings,
                     &SettingsStore::audioCaptureSettingsChanged,
                     input,
                     &QtAudioInput::applySettings);
    return input;
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

GlobalShortcutBinder *WindowsComposition::createGlobalShortcutBinder(QObject *parent) const
{
    return new WinGlobalShortcutBinder(parent);
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
