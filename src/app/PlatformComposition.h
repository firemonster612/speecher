#pragma once

#include "core/EchoCanceller.h"
#include "dictation/DictationPorts.h"
#include "platform/AccessibilityState.h"
#include "platform/MicrophoneAccess.h"

#include <functional>
#include <memory>

namespace speecher {

class SessionKeyGrab;
class GlobalShortcutBinder;
enum class GlobalShortcutRole;
class DesktopColors;
class PopupPositioner;
class WindowBackdrop;
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
    // The microphone for a Recording: dictation's, except where the system's
    // own echo cancellation needs the microphone opened for it.
    virtual AudioInput *createRecordingAudioInput(SettingsStore *settings, QObject *parent) const
    {
        return createAudioInput(settings, parent);
    }
    // Whether a Recording takes system audio as a second stream, "them":
    // wherever createSystemAudioInput captures it, even where echo
    // cancellation cannot start, as the Recording then says why. Otherwise a
    // Recording takes the microphone alone.
    virtual bool recordsSystemAudio() const { return false; }
    // What the speakers play, from the default output, as 16 kHz mono s16 like
    // the microphone; none where Speecher cannot capture it.
    virtual AudioInput *createSystemAudioInput(QObject *parent) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }
    // Takes system audio out of the microphone's for a Recording. None and no
    // warning where the system's own echo cancellation does it, and the
    // Recording's microphone says why it might not; none with why in warning
    // where Speecher's cannot start.
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
    // What the privacy settings say now; asked each time, as the person can
    // change them while Speecher runs.
    virtual MicrophoneAccess microphoneAccess() const { return MicrophoneAccess::NotAsked; }
    // Opens the system's microphone privacy settings, where there are any.
    virtual void openMicrophoneSettings() const {}
    virtual MediaController *createMediaController(QObject *parent) const = 0;
    virtual TargetProvider *createTargetProvider(QObject *parent) const = 0;
    virtual ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const = 0;
    virtual TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const = 0;
    virtual PopupPositioner *createPopupPositioner(QObject *parent) const = 0;
    // The compositor's blur behind a window's translucent part; none where
    // the platform has no such effect.
    virtual WindowBackdrop *createWindowBackdrop(QObject *parent) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }
    // The desktop's own colour scheme, followed as it changes: Plasma's;
    // none elsewhere, where the UI takes the platform palette's accent.
    virtual DesktopColors *createDesktopColors(QObject *parent) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }
    // A binder that keeps its binding in Speecher's settings does so through
    // settings, which must outlive it.
    virtual GlobalShortcutBinder *createGlobalShortcutBinder(GlobalShortcutRole role,
                                                             SettingsStore *settings,
                                                             QObject *parent) const = 0;
    // Takes Escape and Enter while a Dictation Session needs them, on
    // platforms that can stop them reaching the focused app; none elsewhere.
    virtual SessionKeyGrab *createSessionKeyGrab(QObject *parent) const
    {
        Q_UNUSED(parent);
        return nullptr;
    }
    // Whether Escape cancels through a whole Dictation Session, rather than
    // only keeping the original while a selection edit is reviewed.
    virtual bool escapeCancelsDictation() const { return false; }
    // The program and arguments that open the desktop's keyboard shortcut
    // settings; empty where Speecher knows of none.
    virtual QStringList shortcutSettingsCommand() const { return {}; }

    virtual AccessibilityState accessibilityState() const = 0;
    virtual void watchAccessibilityChanges(QObject *context,
                                           std::function<void()> refresh) const
    {
        Q_UNUSED(context);
        Q_UNUSED(refresh);
    }
    virtual bool requestAccessibility(QString *error = nullptr) const = 0;
    virtual bool enableAccessibilityPermanently(QString *error = nullptr) const = 0;
    // Whether setLaunchAtLogin starts Speecher at login: macOS and Windows.
    virtual bool canLaunchAtLogin() const { return false; }
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
