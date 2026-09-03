#include "app/MacComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsCodecs.h"
#include "output/TextDelivery.h"
#include "output/mac/MacPasteDelivery.h"
#include "platform/audio/QtAudioInput.h"
#include "platform/mac/MacGlobalShortcutBinder.h"
#include "platform/mac/MacMediaController.h"
#include "platform/mac/MacPopupPositioner.h"
#include "platform/mac/MacScreenshotContextProvider.h"
#include "platform/mac/MacTargetProvider.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QUrl>

#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioHardwareService.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>
#import <Security/SecCode.h>
#import <ServiceManagement/ServiceManagement.h>

#include <algorithm>

namespace speecher {
namespace {

constexpr auto accessibilityPaneUrl =
    "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";

QString shellQuote(QString value)
{
    return QStringLiteral("'")
        + value.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"))
        + QStringLiteral("'");
}

bool runsFromAppBundle()
{
    NSString *extension = NSBundle.mainBundle.bundleURL.pathExtension;
    return [extension caseInsensitiveCompare:@"app"] == NSOrderedSame;
}

QString serviceStatusName(SMAppServiceStatus status)
{
    switch (status) {
    case SMAppServiceStatusNotRegistered:
        return QStringLiteral("not registered");
    case SMAppServiceStatusEnabled:
        return QStringLiteral("enabled");
    case SMAppServiceStatusRequiresApproval:
        return QStringLiteral("requires approval");
    case SMAppServiceStatusNotFound:
        return QStringLiteral("not found");
    }
    return QStringLiteral("unknown");
}

void logAccessibilityIdentity()
{
    QString signingIdentifier = QStringLiteral("unavailable");
    QString cdhash = QStringLiteral("unavailable");
    SecCodeRef code = nullptr;
    CFDictionaryRef signing = nullptr;
    if (SecCodeCopySelf(kSecCSDefaultFlags, &code) == errSecSuccess
        && SecCodeCopySigningInformation(code,
                                         kSecCSSigningInformation,
                                         &signing) == errSecSuccess) {
        NSString *identifier = (__bridge NSString *)CFDictionaryGetValue(
            signing, kSecCodeInfoIdentifier);
        NSData *unique = (__bridge NSData *)CFDictionaryGetValue(signing,
                                                                 kSecCodeInfoUnique);
        if (identifier) {
            signingIdentifier = QString::fromUtf8(identifier.UTF8String);
        }
        if (unique) {
            cdhash = QString::fromLatin1(
                QByteArray(static_cast<const char *>(unique.bytes), int(unique.length)).toHex());
        }
    }
    if (signing) {
        CFRelease(signing);
    }
    if (code) {
        CFRelease(code);
    }

    qInfo().noquote() << "macOS accessibility identity bundle=\""
                      + QString::fromUtf8(NSBundle.mainBundle.bundlePath.UTF8String)
                      + "\" trusted=" + QString::number(AXIsProcessTrusted())
                      + " signingIdentifier=\"" + signingIdentifier
                      + "\" cdhash=" + cdhash;
}

} // namespace

MacComposition::MacComposition()
{
    logAccessibilityIdentity();
}

QString MacComposition::outputSummary() const
{
    return QStringLiteral("Automatic: keyboard paste (Cmd+V), Qt clipboard fallback");
}

QString MacComposition::primaryOutputStatus() const
{
    return MacPasteDelivery::isAvailable()
        ? QStringLiteral("Keyboard paste available")
        : QStringLiteral("Qt clipboard only until Accessibility is granted");
}

QString MacComposition::ipcListenName() const
{
    return appSocketName();
}

QStringList MacComposition::ipcConnectCandidates() const
{
    return {appSocketName(), executablePathSocketName()};
}

QString MacComposition::detachedExecutablePath() const
{
    return QCoreApplication::applicationFilePath();
}

QList<AudioInputDeviceInfo> MacComposition::availableAudioInputDevices() const
{
    return QtAudioInput::availableInputDevices();
}

AudioInput *MacComposition::createAudioInput(SettingsStore *settings, QObject *parent) const
{
    auto *input = new QtAudioInput(settings->audioCaptureSettings(), parent);
    QObject::connect(settings,
                     &SettingsStore::audioCaptureSettingsChanged,
                     input,
                     &QtAudioInput::applySettings);
    return input;
}

MediaController *MacComposition::createMediaController(QObject *parent) const
{
    return new MacMediaController(parent);
}

TargetProvider *MacComposition::createTargetProvider(QObject *parent) const
{
    return new MacTargetProvider(parent);
}

ScreenshotContextProvider *MacComposition::createScreenshotContextProvider(QObject *parent) const
{
    return new MacScreenshotContextProvider(parent);
}

TextDeliveryAdapter *MacComposition::createTextDelivery(TargetProvider *targetProvider, QObject *parent) const
{
    return new TextDelivery(targetProvider, parent);
}

PopupPositioner *MacComposition::createPopupPositioner(QObject *parent) const
{
    return new MacPopupPositioner(parent);
}

GlobalShortcutBinder *MacComposition::createGlobalShortcutBinder(QObject *parent) const
{
    return new MacGlobalShortcutBinder(parent);
}

AccessibilityState MacComposition::accessibilityState() const
{
    // The macOS grant is recorded per app signature and survives restarts, so
    // there is no weaker session-only state to distinguish. Reporting
    // persistent == enabled is therefore deliberate rather than a stand-in: a
    // trusted process stays trusted. ApplicationController::runDeferredStartup
    // relies on it, skipping requestAccessibility() unless the grant already
    // exists, which is what keeps launch from raising a permission prompt.
    const bool trusted = AXIsProcessTrusted();
    return {true, trusted, trusted};
}

void MacComposition::watchAccessibilityChanges(QObject *context,
                                               std::function<void()> refresh) const
{
    id token = [NSNotificationCenter.defaultCenter
        addObserverForName:NSApplicationDidBecomeActiveNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification *) {
                    refresh();
                }];
    QObject::connect(context, &QObject::destroyed, [token] {
        [NSNotificationCenter.defaultCenter removeObserver:token];
    });
}

bool MacComposition::requestAccessibility(QString *error) const
{
    NSDictionary *options = @{(id)kAXTrustedCheckOptionPrompt: @YES};
    if (AXIsProcessTrustedWithOptions((CFDictionaryRef)options)) {
        return true;
    }
    if (error) {
        // The grant is not delivered to a running process; posting events keeps
        // failing until the next launch.
        *error = QStringLiteral(
            "Allow Speecher under Privacy & Security > Accessibility, then restart Speecher");
    }
    return false;
}

bool MacComposition::enableAccessibilityPermanently(QString *error) const
{
    if (QDesktopServices::openUrl(QUrl(QString::fromLatin1(accessibilityPaneUrl)))) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Could not open the Privacy & Security > Accessibility settings pane");
    }
    return false;
}

bool MacComposition::setLaunchAtLogin(bool enabled, QString *error) const
{
    if (!runsFromAppBundle()) {
        if (error) {
            *error = QStringLiteral("Launch at login requires Speecher to run from an app bundle");
        }
        SettingsCodecs settings;
        settings.setLaunchAtLogin(false);
        return false;
    }

    SMAppService *service = SMAppService.mainAppService;
    const SMAppServiceStatus status = service.status;
    if ((enabled && (status == SMAppServiceStatusEnabled
                     || status == SMAppServiceStatusRequiresApproval))
        || (!enabled && status == SMAppServiceStatusNotRegistered)) {
        qInfo().noquote() << "launch at login status=" + serviceStatusName(status);
        return true;
    }

    NSError *serviceError = nil;
    const bool changed = enabled
        ? [service registerAndReturnError:&serviceError]
        : [service unregisterAndReturnError:&serviceError];
    qInfo().noquote() << "launch at login status=" + serviceStatusName(service.status);
    if (!changed && error) {
        *error = serviceError
            ? QString::fromUtf8(serviceError.localizedDescription.UTF8String)
            : QStringLiteral("macOS did not change the launch at login service");
    }
    if (!changed) {
        SettingsCodecs settings;
        settings.setLaunchAtLogin(launchAtLoginEnabled());
    }
    return changed;
}

bool MacComposition::launchAtLoginEnabled() const
{
    return runsFromAppBundle()
        && SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
}

std::optional<float> MacComposition::inputVolume() const
{
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    const AudioObjectPropertyAddress defaultInput{
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                   &defaultInput,
                                   0,
                                   nullptr,
                                   &size,
                                   &device) != noErr
        || device == kAudioObjectUnknown) {
        return std::nullopt;
    }

    // Qt device ids do not map directly to AudioDeviceID values. Until that
    // mapping exists, this intentionally reports only the default input.
    const AudioObjectPropertyAddress volumeProperty{
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain,
    };
    if (!AudioObjectHasProperty(device, &volumeProperty)) {
        return std::nullopt;
    }
    Float32 volume = 0.0f;
    size = sizeof(volume);
    if (AudioObjectGetPropertyData(device,
                                   &volumeProperty,
                                   0,
                                   nullptr,
                                   &size,
                                   &volume) != noErr) {
        return std::nullopt;
    }
    return std::clamp(volume, 0.0f, 1.0f);
}

void MacComposition::relaunch() const
{
    QDir bundle(QCoreApplication::applicationDirPath());
    const bool inBundle = bundle.dirName() == QStringLiteral("MacOS")
        && bundle.cdUp()
        && bundle.dirName() == QStringLiteral("Contents")
        && bundle.cdUp()
        && bundle.dirName().endsWith(QStringLiteral(".app"), Qt::CaseInsensitive);
    const QString command = inBundle
        ? QStringLiteral("sleep 1; open -n %1").arg(shellQuote(bundle.absolutePath()))
        : QStringLiteral("sleep 1; exec %1").arg(
              shellQuote(QCoreApplication::applicationFilePath()));
    QProcess::startDetached(QStringLiteral("/bin/sh"),
                            {QStringLiteral("-c"), command});
    QCoreApplication::quit();
}

std::shared_ptr<const MacComposition> macComposition()
{
    return std::make_shared<MacComposition>();
}

} // namespace speecher
