#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QProgressBar;
class QPushButton;

namespace speecher {

class ApplicationController;
class AudioInput;
class LinuxComposition;
class ProviderRegistry;
class SettingsStore;

class WelcomeSetupPage final : public QWidget {
public:
    explicit WelcomeSetupPage(QWidget *parent = nullptr);
};

class ClaudeSignInSetupPage final : public QWidget {
public:
    explicit ClaudeSignInSetupPage(SettingsStore &settings, QWidget *parent = nullptr);

private:
    void checkCredentials();

    SettingsStore &m_settings;
    QLabel *m_status;
};

class MicrophoneSetupPage final : public QWidget {
public:
    MicrophoneSetupPage(SettingsStore &settings,
                        const LinuxComposition &platform,
                        QWidget *parent = nullptr);
    ~MicrophoneSetupPage() override;

    void setActive(bool active);

private:
    void refreshDevices();
    void startMeter();

    SettingsStore &m_settings;
    const LinuxComposition &m_platform;
    AudioInput *m_input = nullptr;
    QComboBox *m_device;
    QProgressBar *m_level;
    QLabel *m_status;
    bool m_active = false;
};

class AccessibilitySetupPage final : public QWidget {
public:
    explicit AccessibilitySetupPage(ApplicationController &controller,
                                    QWidget *parent = nullptr);

private:
    void updateState(bool supported, bool enabled, bool persistent);

    ApplicationController &m_controller;
    QLabel *m_status;
    QPushButton *m_enable;
};

class TextDeliverySetupPage final : public QWidget {
public:
    explicit TextDeliverySetupPage(SettingsStore &settings, QWidget *parent = nullptr);

    bool needsSignIn() const;

private:
    void refreshStatus();
    void runSetup();

    SettingsStore &m_settings;
    QLabel *m_status;
    QPushButton *m_setup;
    QProgressBar *m_progress;
    QCheckBox *m_restoreClipboard;
    QComboBox *m_format;
    bool m_needsSignIn = false;
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
    void applyShortcut();

private:
    ApplicationController &m_controller;
    QCheckBox *m_createShortcut = nullptr;
    QKeySequenceEdit *m_shortcut = nullptr;
    QLabel *m_shortcutStatus;
    QLabel *m_signInNote;
};

} // namespace speecher
