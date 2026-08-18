#pragma once

#include "dictation/DictationPorts.h"
#include "platform/AccessibilityState.h"

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
    virtual QString primaryOutputStatus() const = 0;

    virtual QList<AudioInputDeviceInfo> availableAudioInputDevices() const = 0;
    virtual AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const = 0;
    virtual MediaController *createMediaController(QObject *parent) const = 0;
    virtual TargetProvider *createTargetProvider(QObject *parent) const = 0;
    virtual ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const = 0;
    virtual TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const = 0;
    virtual PopupPositioner *createPopupPositioner(QObject *parent) const = 0;
    virtual GlobalShortcutBinder *createGlobalShortcutBinder(QObject *parent) const = 0;

    virtual AccessibilityState accessibilityState() const = 0;
    virtual bool requestAccessibility(QString *error = nullptr) const = 0;
    virtual bool enableAccessibilityPermanently(QString *error = nullptr) const = 0;
};

std::shared_ptr<const PlatformComposition> platformComposition();

} // namespace speecher
