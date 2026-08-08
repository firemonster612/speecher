#pragma once

#include "dictation/DictationPorts.h"

#include <memory>

namespace speecher {

class PopupPositioner;
class SettingsStore;

class SingleInstancePlatform {
public:
    virtual ~SingleInstancePlatform() = default;
    virtual QString ipcListenName() const = 0;
    virtual QStringList ipcConnectCandidates() const = 0;
    virtual QString detachedExecutablePath() const = 0;
};

class LinuxComposition final : public SingleInstancePlatform {
public:
    QString id() const;
    QString outputSummary() const;
    QString primaryOutputStatus() const;
    QString ipcListenName() const override;
    QStringList ipcConnectCandidates() const override;
    QString detachedExecutablePath() const override;

    QList<AudioInputDeviceInfo> availableAudioInputDevices() const;
    AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const;
    MediaController *createMediaController(QObject *parent) const;
    TargetProvider *createTargetProvider(QObject *parent) const;
    ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const;
    TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const;
    PopupPositioner *createPopupPositioner(QObject *parent) const;
};

std::shared_ptr<const LinuxComposition> linuxComposition();

} // namespace speecher
