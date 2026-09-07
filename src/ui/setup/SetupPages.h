#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QShowEvent;
class QProgressBar;
class QPushButton;

namespace speecher {

class ApplicationController;
class AudioInput;
class PlatformComposition;
class ProviderRegistry;
class SettingsStore;
struct ProviderStat;

// The margin every assistant page keeps around its content, so a page built
// elsewhere can match when it is shown as one.
int setupPageMargin();

// The label/value facts describing a provider, shown under the provider picker
// so the choice is explained in place.
class ProviderStatsBlock final : public QWidget {
public:
    explicit ProviderStatsBlock(QWidget *parent = nullptr);
    void setStats(const QVector<ProviderStat> &stats);

private:
    QFormLayout *m_rows;
};

class WelcomeSetupPage final : public QWidget {
public:
    explicit WelcomeSetupPage(QWidget *parent = nullptr);
};

class SpeechProviderSetupPage final : public QWidget {
    Q_OBJECT

public:
    SpeechProviderSetupPage(SettingsStore &settings,
                            ProviderRegistry &providers,
                            QWidget *parent = nullptr);

    // The setup assistant holds Next until the chosen service checked out.
    bool ready() const { return m_ready; }

signals:
    void readyChanged();

private:
    void updateProvider();
    void checkProvider();
    void setReady(bool ready);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QComboBox *m_provider;
    ProviderStatsBlock *m_stats;
    QLabel *m_hint;
    QLabel *m_status;
    QPushButton *m_checkAgain;
    quint64 m_checkGeneration = 0;
    bool m_ready = false;
};

class MicrophoneSetupPage final : public QWidget {
    Q_OBJECT

public:
    MicrophoneSetupPage(SettingsStore &settings,
                        const PlatformComposition &platform,
                        QWidget *parent = nullptr);
    ~MicrophoneSetupPage() override;

    void setActive(bool active);

    // The setup assistant holds Next until the meter has heard something.
    bool inputDetected() const { return m_inputDetected; }

signals:
    void inputDetectedChanged();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refreshDevices();
    void startMeter();

    SettingsStore &m_settings;
    const PlatformComposition &m_platform;
    AudioInput *m_input = nullptr;
    QComboBox *m_device;
    QProgressBar *m_level;
    QLabel *m_status;
    bool m_active = false;
    bool m_devicesLoaded = false;
    bool m_inputDetected = false;
};

class AccessibilitySetupPage final : public QWidget {
    Q_OBJECT

public:
    explicit AccessibilitySetupPage(ApplicationController &controller,
                                    QWidget *parent = nullptr);

    // Complete once accessibility is on, or when there is nothing this build
    // or platform lets the user do about it.
    bool stepComplete() const;

signals:
    void stepCompleteChanged();

private:
    void updateState(bool supported, bool enabled, bool persistent);
    void refreshFromController();

    ApplicationController &m_controller;
    QLabel *m_status;
    // What the last grant request answered. Outranks the polled state until the
    // user acts again, so the poll cannot wipe the reply to their click.
    QString m_lastError;
    QPushButton *m_enable;
    bool m_supported = false;
    bool m_enabled = false;
};

class TextDeliverySetupPage final : public QWidget {
    Q_OBJECT

public:
    explicit TextDeliverySetupPage(SettingsStore &settings, QWidget *parent = nullptr);

    bool needsSignIn() const;

    // Complete when the virtual keyboard works (or waits on a sign-out), or
    // the user explicitly chose clipboard-only paste. Platforms without a
    // virtual keyboard to install are always complete.
    bool stepComplete() const;

signals:
    void signInRequirementChanged(bool required);
    void stepCompleteChanged();

private:
    void refreshStatus();
    void runSetup();

    SettingsStore &m_settings;
    QLabel *m_status;
    QPushButton *m_setup;
    QProgressBar *m_progress;
    QCheckBox *m_clipboardOnly;
    QCheckBox *m_restoreClipboard;
    QComboBox *m_format;
};

class RefinementSetupPage final : public QWidget {
public:
    RefinementSetupPage(SettingsStore &settings,
                        ProviderRegistry &providers,
                        QWidget *parent = nullptr);

private:
    void updateProviderStats();
    void updateFastModeControl();

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QComboBox *m_provider;
    ProviderStatsBlock *m_stats;
    QCheckBox *m_fastMode;
    QLabel *m_fastModeHint;
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

protected:
    void showEvent(QShowEvent *event) override;

private:
    ApplicationController &m_controller;
#ifdef Q_OS_LINUX
    void updateLinuxShortcutInstruction();
    QLabel *m_manualCommand = nullptr;
#endif
    QLabel *m_shortcutStatus;
    QLabel *m_signInNote;
};

} // namespace speecher
