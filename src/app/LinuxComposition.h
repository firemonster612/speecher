#pragma once

#include "app/PlatformComposition.h"

#include <memory>

namespace speecher {

class LinuxComposition final : public PlatformComposition {
public:
    QString outputSummary() const override;
    QString ipcListenName() const override;
    QStringList ipcConnectCandidates() const override;
    QString detachedExecutablePath() const override;

    QList<AudioInputDeviceInfo> availableAudioInputDevices() const override;
    AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const override;
    MediaController *createMediaController(QObject *parent) const override;
    TargetProvider *createTargetProvider(QObject *parent) const override;
    ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const override;
    TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const override;
    PopupPositioner *createPopupPositioner(QObject *parent) const override;
    GlobalShortcutBinder *createGlobalShortcutBinder(QObject *parent) const override;

    AccessibilityState accessibilityState() const override;
    bool requestAccessibility(QString *error = nullptr) const override;
    bool enableAccessibilityPermanently(QString *error = nullptr) const override;
    bool setLaunchAtLogin(bool enabled, QString *error = nullptr) const override;
    bool launchAtLoginEnabled() const override;
};

std::shared_ptr<const LinuxComposition> linuxComposition();

} // namespace speecher
