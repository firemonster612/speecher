#pragma once

#include "dictation/DictationInterfaces.h"

#include <memory>

class QWidget;

namespace speecher {

class SettingsStore;

class PopupPositioner : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void configurePopup(QWidget *widget) = 0;
    virtual void positionBottomCenter(QWidget *widget) = 0;
};

class PlatformIntegration {
public:
    virtual ~PlatformIntegration() = default;

    virtual QString id() const = 0;
    virtual QString outputSummary() const = 0;
    virtual QString primaryOutputStatus() const = 0;
    virtual QString ipcListenName() const = 0;
    virtual QStringList ipcConnectCandidates() const = 0;
    virtual QString detachedExecutablePath() const = 0;

    virtual QList<AudioInputDeviceInfo> availableAudioInputDevices() const = 0;
    virtual AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const = 0;
    virtual MediaController *createMediaController(QObject *parent) const = 0;
    virtual TextDeliveryAdapter *createTextDelivery(QObject *parent) const = 0;
    virtual PopupPositioner *createPopupPositioner(QObject *parent) const = 0;
};

class PlatformFactory {
public:
    static std::shared_ptr<const PlatformIntegration> create();
};

} // namespace speecher
