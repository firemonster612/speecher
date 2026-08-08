#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace speecher {

class PlatformIntegration;

class AudioSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit AudioSettingsPage(const PlatformIntegration &platform,
                               QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;

signals:
    void changed();

private:
    void refreshAudioDeviceList(const QString &selectedDeviceId);
    void updateAudioControls();

    const PlatformIntegration &m_platform;
    QComboBox *m_audioDevice;
    QComboBox *m_captureMode;
    QCheckBox *m_vadEnabled;
    QSpinBox *m_preRollMs;
    QSpinBox *m_postRollMs;
    QSpinBox *m_readinessTimeoutMs;
    QSpinBox *m_vadThreshold;
};

} // namespace speecher
