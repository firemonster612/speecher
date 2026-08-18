#include "app/MacComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
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
#include <QUrl>

#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

namespace speecher {
namespace {

constexpr auto accessibilityPaneUrl =
    "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";

} // namespace

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
    // there is no weaker session-only state to distinguish.
    const bool trusted = AXIsProcessTrusted();
    return {true, trusted, trusted};
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

std::shared_ptr<const MacComposition> macComposition()
{
    return std::make_shared<MacComposition>();
}

} // namespace speecher
