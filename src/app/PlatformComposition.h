#pragma once

#include "dictation/DictationPorts.h"
#include "platform/AccessibilityState.h"

#include <functional>
#include <memory>

namespace speecher {

class GlobalShortcutBinder;
class PopupPositioner;
class SettingsStore;

class SingleInstancePlatform {
public:
    virtual ~SingleInstancePlatform() = default;
    virtual QString ipcListenName() const = 0;
    virtual QStringList ipcConnectCandidates() const = 0;
    virtual QString detachedExecutablePath() const = 0;
};

// Everything Speecher needs from the desktop it runs on. Exactly one
// implementation is compiled per platform; see platformComposition().
class PlatformComposition : public SingleInstancePlatform {
public:
    virtual QString outputSummary() const = 0;

    virtual QList<AudioInputDeviceInfo> availableAudioInputDevices() const = 0;
    virtual AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const = 0;
    virtual MediaController *createMediaController(QObject *parent) const = 0;
    virtual TargetProvider *createTargetProvider(QObject *parent) const = 0;
    virtual ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const = 0;
    virtual TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const = 0;
    virtual PopupPositioner *createPopupPositioner(QObject *parent) const = 0;
    virtual GlobalShortcutBinder *createGlobalShortcutBinder(QObject *parent) const = 0;

    virtual AccessibilityState accessibilityState() const = 0;
    virtual void watchAccessibilityChanges(QObject *context,
                                           std::function<void()> refresh) const
    {
        Q_UNUSED(context);
        Q_UNUSED(refresh);
    }
    virtual bool requestAccessibility(QString *error = nullptr) const = 0;
    virtual bool enableAccessibilityPermanently(QString *error = nullptr) const = 0;
    virtual bool setLaunchAtLogin(bool enabled, QString *error = nullptr) const
    {
        Q_UNUSED(enabled);
        Q_UNUSED(error);
        return true;
    }
    virtual bool launchAtLoginEnabled() const { return false; }
    virtual std::optional<float> inputVolume() const { return std::nullopt; }
    virtual void relaunch() const {}
};

std::shared_ptr<const PlatformComposition> platformComposition();

} // namespace speecher
