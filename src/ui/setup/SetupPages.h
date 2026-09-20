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
class QRadioButton;
class QTimer;

namespace speecher {

class ApplicationController;
class AudioInput;
class PlatformComposition;
class ProviderRegistry;
class SettingsStore;
struct ProviderStat;
struct RefinementPrepareResult;
struct SpeechPrepareResult;

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

// A speech or refinement provider the user can pick, with the readiness probe's
// verdict shown next to it.
struct ProviderOptionRow {
    QString id;
    QString label;
    QRadioButton *button = nullptr;
    QLabel *status = nullptr;
    bool probed = false;
    bool ok = false;
    QString message;
};

class WelcomeSetupPage final : public QWidget {
    Q_OBJECT

public:
    WelcomeSetupPage(SettingsStore &settings,
                     ProviderRegistry &providers,
                     QWidget *parent = nullptr);

    // Setup cannot succeed without one of the provider CLIs signed in, so the
    // assistant holds Next until the probe finds one.
    bool ready() const { return m_ready; }
    // Re-run the probe while the page is off screen, so a sign-in that lapsed
    // mid-wizard closes the gate before Finish commits.
    void recheck();

signals:
    void readyChanged();
    // Every provider in this round has answered. The assistant waits for it
    // before probing the same providers again from another page.
    void checkFinished();

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct CredentialRow {
        QString providerId;
        QLabel *status = nullptr;
        QLabel *hint = nullptr;
        bool found = false;
    };

    void checkCredentials();
    void showCredential(int index, bool found);
    void setReady(bool ready);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QList<CredentialRow> m_rows;
    quint64 m_checkGeneration = 0;
    int m_checksOutstanding = 0;
    bool m_ready = false;
};

class SpeechProviderSetupPage final : public QWidget {
    Q_OBJECT

public:
    SpeechProviderSetupPage(SettingsStore &settings,
                            ProviderRegistry &providers,
                            QWidget *parent = nullptr);

    // The setup assistant holds Next until the chosen service checked out.
    bool ready() const { return m_ready; }
    // Re-run the probes while the page is off screen; see WelcomeSetupPage.
    void recheck();

signals:
    void readyChanged();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void selectProvider(const QString &providerId);
    void checkProviders();
    void probeProvider(int index, quint64 generation);
    void finishProbe(int index, quint64 generation, const SpeechPrepareResult &result);
    void showSelectedProvider();
    void autoSelectReadyProvider();
    int selectedIndex() const;
    void setReady(bool ready);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QList<ProviderOptionRow> m_options;
    ProviderStatsBlock *m_stats;
    QLabel *m_hint;
    QLabel *m_status;
    QPushButton *m_checkAgain;
    quint64 m_checkGeneration = 0;
    int m_pendingProbes = 0;
    // Auto-selecting a ready provider is a one-time courtesy on the first
    // round of probes, and never overrules a choice the user just made.
    bool m_autoSelectDone = false;
    bool m_userSelected = false;
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
    void setInputDetected(bool detected);

    SettingsStore &m_settings;
    const PlatformComposition &m_platform;
    AudioInput *m_input = nullptr;
    QComboBox *m_device;
    QProgressBar *m_level;
    QLabel *m_status;
    QTimer *m_noInputTimer;
    QMetaObject::Connection m_levelConnection;
    // Levels from the device the meter just left arrive for another moment;
    // only the run that started them may satisfy the gate.
    quint64 m_meterGeneration = 0;
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

protected:
    void showEvent(QShowEvent *event) override;

private:
    void selectProvider(const QString &providerId);
    void checkProviders();
    void probeProvider(int index, quint64 generation);
    void finishProbe(int index, quint64 generation, const RefinementPrepareResult &result);
    void showSelectedProvider();
    void autoSelectReadyProvider();
    void updateProviderStats();
    void updateFastModeControl();
    int selectedIndex() const;
    QString selectedProviderId() const;

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    // The provider rows, then the None row, which needs no readiness probe.
    QList<ProviderOptionRow> m_options;
    QRadioButton *m_none;
    ProviderStatsBlock *m_stats;
    QLabel *m_warning;
    QCheckBox *m_fastMode;
    QLabel *m_fastModeHint;
    quint64 m_checkGeneration = 0;
    int m_pendingProbes = 0;
    bool m_autoSelectDone = false;
    bool m_userSelected = false;
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
    QLabel *m_trayNote = nullptr;
#endif
    QLabel *m_shortcutStatus;
    QLabel *m_signInNote;
};

} // namespace speecher
