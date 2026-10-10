#pragma once

#include "core/EchoCanceller.h"
#include "dictation/DictationPorts.h"
#include "platform/AccessibilityState.h"

#include <functional>
#include <memory>

namespace speecher {

class CancelKeyGrab;
class GlobalShortcutBinder;
enum class GlobalShortcutRole;
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
    // What the speakers play, from the default output, as 16 kHz mono s16 like
    // the microphone; none where Speecher cannot capture it.
    virtual AudioInput *createSystemAudioInput(QObject *parent) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }
    // Takes system audio out of the microphone's for a Recording. None and no
    // warning where the system's own echo cancellation does it; none with
    // why in warning where Speecher's cannot start.
    virtual std::unique_ptr<EchoCanceller> createEchoCanceller(QString *warning) const
    {
        Q_UNUSED(warning);
        return nullptr;
    }
    // Complete on the caller's thread, and never after context is destroyed.
    virtual void requestMicrophoneAccess(QObject *context, std::function<void(bool)> completed) const
    {
        Q_UNUSED(context);
        completed(true);
    }
    virtual MediaController *createMediaController(QObject *parent) const = 0;
    virtual TargetProvider *createTargetProvider(QObject *parent) const = 0;
    virtual ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const = 0;
    virtual TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const = 0;
    virtual PopupPositioner *createPopupPositioner(QObject *parent) const = 0;
    virtual GlobalShortcutBinder *createGlobalShortcutBinder(GlobalShortcutRole role,
                                                             QObject *parent) const = 0;
    // Takes Escape while a Dictation Session can be cancelled, on platforms
    // that can stop it reaching the focused app; none elsewhere.
    virtual CancelKeyGrab *createCancelKeyGrab(QObject *parent) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }

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
