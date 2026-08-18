#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QHideEvent;
class QKeySequenceEdit;
class QLabel;
class QShowEvent;
class QProgressBar;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace speecher {

class ApplicationController;
class AudioInput;
class PlatformComposition;
class ProviderRegistry;
class SettingsStore;

class WelcomeSetupPage final : public QWidget {
public:
    explicit WelcomeSetupPage(QWidget *parent = nullptr);
};

class SpeechProviderSetupPage final : public QWidget {
public:
    SpeechProviderSetupPage(SettingsStore &settings,
                            ProviderRegistry &providers,
                            QWidget *parent = nullptr);

private:
    void updateProvider();
    void checkProvider();

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QComboBox *m_provider;
    QLabel *m_hint;
    QLabel *m_status;
};

class MicrophoneSetupPage final : public QWidget {
public:
    MicrophoneSetupPage(SettingsStore &settings,
                        const PlatformComposition &platform,
                        QWidget *parent = nullptr);
    ~MicrophoneSetupPage() override;

    void setActive(bool active);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refreshDevices();
    void startMeter();
#ifdef Q_OS_MACOS
    void addMicrophonePermissionControls(QVBoxLayout *layout);
    void refreshMicrophonePermission();
#endif

    SettingsStore &m_settings;
    const PlatformComposition &m_platform;
    AudioInput *m_input = nullptr;
    QComboBox *m_device;
    QProgressBar *m_level;
    QLabel *m_status;
#ifdef Q_OS_MACOS
    QLabel *m_permissionStatus = nullptr;
    QPushButton *m_allowMicrophone = nullptr;
    QPushButton *m_openMicrophoneSettings = nullptr;
#endif
    bool m_active = false;
    bool m_devicesLoaded = false;
};

class AccessibilitySetupPage final : public QWidget {
public:
    explicit AccessibilitySetupPage(ApplicationController &controller,
                                    QWidget *parent = nullptr);

#ifdef Q_OS_MACOS
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
#endif

private:
    void updateState(bool supported, bool enabled, bool persistent);

    ApplicationController &m_controller;
    QLabel *m_status;
    QPushButton *m_enable;
#ifdef Q_OS_MACOS
    QPushButton *m_request;
    QTimer *m_poll;
#endif
};

class TextDeliverySetupPage final : public QWidget {
    Q_OBJECT

public:
    explicit TextDeliverySetupPage(SettingsStore &settings, QWidget *parent = nullptr);

    bool needsSignIn() const;

signals:
    void signInRequirementChanged(bool required);

private:
    void refreshStatus();
    void runSetup();

    SettingsStore &m_settings;
    QLabel *m_status;
    QPushButton *m_setup;
    QProgressBar *m_progress;
    QCheckBox *m_restoreClipboard;
    QComboBox *m_format;
};

class RefinementSetupPage final : public QWidget {
public:
    RefinementSetupPage(SettingsStore &settings,
                        ProviderRegistry &providers,
                        QWidget *parent = nullptr);

private:
    SettingsStore &m_settings;
    QComboBox *m_provider;
};

class WritingProfilesSetupPage final : public QWidget {
public:
    explicit WritingProfilesSetupPage(SettingsStore &settings,
                                      QWidget *parent = nullptr);

private:
    struct ProfileControls {
        WritingProfile profile;
        QComboBox *cleanup;
        QComboBox *tone;
    };

    void saveProfiles();

    SettingsStore &m_settings;
    QComboBox *m_defaultProfile;
    QList<ProfileControls> m_profiles;
};

class FinishSetupPage final : public QWidget {
public:
    explicit FinishSetupPage(ApplicationController &controller,
                             QWidget *parent = nullptr);

    void setSignInRequired(bool required);
    bool applyShortcut();

protected:
    void showEvent(QShowEvent *event) override;

private:
    ApplicationController &m_controller;
    QCheckBox *m_createShortcut = nullptr;
    QKeySequenceEdit *m_shortcut = nullptr;
    QLabel *m_shortcutStatus;
    QLabel *m_signInNote;
    bool m_shortcutFailureAcknowledged = false;
    bool m_shortcutLoaded = false;
};

} // namespace speecher
