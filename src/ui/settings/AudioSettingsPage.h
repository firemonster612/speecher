#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QMediaDevices;
class QSpinBox;

namespace speecher {

class PlatformComposition;
class ProviderRegistry;

class AudioSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit AudioSettingsPage(const PlatformComposition &platform,
                               const ProviderRegistry &providers,
                               QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;

signals:
    void changed();

private:
    void refreshAudioDeviceList(const QString &selectedDeviceId);
    void updateAudioControls();

    const PlatformComposition &m_platform;
    QMediaDevices *m_mediaDevices = nullptr;
    QComboBox *m_speechProvider;
    QComboBox *m_audioDevice;
    QComboBox *m_captureMode;
    QCheckBox *m_vadEnabled;
    QSpinBox *m_preRollMs;
    QSpinBox *m_postRollMs;
    QSpinBox *m_readinessTimeoutMs;
    QSpinBox *m_vadThreshold;
};

} // namespace speecher
