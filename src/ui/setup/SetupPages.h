#pragma once

#include "core/AppSettings.h"

#include <QList>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
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

// Puts "Step N of M" above a page's content, right-aligned. The wizard owns
// the numbering, so it marks the pages once it knows how many there are.
void setSetupStepCounter(QWidget *page, int step, int total);

// A provider mark (ChatGPT or Claude) at the given point size, or an empty
// pixmap for a provider with no mark of its own.
QPixmap providerMark(const QString &providerId, int size, qreal devicePixelRatio);

// Shows or hides a row of a settings card along with the hairline above it,
// which would otherwise be left behind as a gap where the row was.
void setCardRowVisible(QWidget *row, bool visible);

// What one wizard step reports to the Ready page.
struct SetupStepStatus {
    QString name;
    bool ok = true;
    // Why the step is unfinished when it is not ok, otherwise what was chosen.
    QString detail;
};

// A wizard step that can explain itself on the Ready page, so each step owns
// its own wording rather than the Ready page keeping a second copy.
class SetupStep {
public:
    virtual ~SetupStep() = default;

    // One line saying why this step is unfinished.
    virtual QString blockedReason() const { return QString(); }
    // What the user chose here, for the completed checklist. Empty for a step
    // with nothing to report back.
    virtual QString readySummary() const { return QString(); }
};

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

class WelcomeSetupPage final : public QWidget, public SetupStep {
    Q_OBJECT

public:
    WelcomeSetupPage(SettingsStore &settings,
                     ProviderRegistry &providers,
                     QWidget *parent = nullptr);

    QString blockedReason() const override;

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
    void showCliproxyCredential(bool found);
    void updateReady();
    void setReady(bool ready);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QList<CredentialRow> m_rows;
    // Accounts saved by CLI Proxy API can power dictation too, so they open
    // the gate like a provider CLI sign-in does.
    QLabel *m_cliproxyStatus = nullptr;
    QLabel *m_cliproxyHint = nullptr;
    bool m_cliproxyFound = false;
    quint64 m_checkGeneration = 0;
    int m_checksOutstanding = 0;
    bool m_ready = false;
};

class SpeechProviderSetupPage final : public QWidget, public SetupStep {
    Q_OBJECT

public:
    SpeechProviderSetupPage(SettingsStore &settings,
                            ProviderRegistry &providers,
                            QWidget *parent = nullptr);

    QString blockedReason() const override;
    QString readySummary() const override;

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
    void updateSignInControls();
    void populateCliproxyAccounts();
    void reprobeSelectedProvider();
    int selectedIndex() const;
    void setReady(bool ready);

    SettingsStore &m_settings;
    ProviderRegistry &m_providers;
    QList<ProviderOptionRow> m_options;
    // Where the selected service's sign-in comes from, so someone whose only
    // account lives in CLI Proxy API can finish setup without visiting
    // Settings first.
    QWidget *m_signInSection = nullptr;
    QComboBox *m_signInSource = nullptr;
    QComboBox *m_cliproxyAccount = nullptr;
    QLineEdit *m_cliproxyDir = nullptr;
    QWidget *m_cliproxyAccountRow = nullptr;
    QWidget *m_cliproxyDirRow = nullptr;
    QCheckBox *m_accuracyPass;
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

class MicrophoneSetupPage final : public QWidget, public SetupStep {
    Q_OBJECT

public:
    MicrophoneSetupPage(SettingsStore &settings,
                        const PlatformComposition &platform,
                        QWidget *parent = nullptr);
    ~MicrophoneSetupPage() override;

    void setActive(bool active);

    QString blockedReason() const override;
    QString readySummary() const override;

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

class AccessibilitySetupPage final : public QWidget, public SetupStep {
    Q_OBJECT

public:
    explicit AccessibilitySetupPage(ApplicationController &controller,
                                    QWidget *parent = nullptr);

    // Complete once accessibility is on, or when there is nothing this build
    // or platform lets the user do about it.
    bool stepComplete() const;

    QString blockedReason() const override;

signals:
    void stepCompleteChanged();

private:
    void updateState(bool supported, bool enabled, bool persistent);
    void refreshFromController();
    void showCapabilities(bool allowed);

    ApplicationController &m_controller;
    QLabel *m_status;
    // The two things the permission actually buys, each with its own verdict.
    QList<QLabel *> m_capabilities;
    // What the last grant request answered. Outranks the polled state until the
    // user acts again, so the poll cannot wipe the reply to their click.
    QString m_lastError;
    QPushButton *m_enable;
    bool m_supported = false;
    bool m_enabled = false;
};

class TextDeliverySetupPage final : public QWidget, public SetupStep {
    Q_OBJECT

public:
    explicit TextDeliverySetupPage(SettingsStore &settings, QWidget *parent = nullptr);

    bool needsSignIn() const;

    QString blockedReason() const override;
    QString readySummary() const override;

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
    // The card row carrying the opt-out; hiding the box alone would leave its
    // wrapping caption and separator behind.
    QWidget *m_clipboardOnlyRow = nullptr;
    QCheckBox *m_restoreClipboard;
    QComboBox *m_format;
};

class RefinementSetupPage final : public QWidget, public SetupStep {
public:
    RefinementSetupPage(SettingsStore &settings,
                        ProviderRegistry &providers,
                        QWidget *parent = nullptr);

    QString readySummary() const override;

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
    Q_OBJECT

public:
    explicit FinishSetupPage(ApplicationController &controller,
                             QWidget *parent = nullptr);

    void setSignInRequired(bool required);
    // The wizard's steps in order. Any step that is not ok turns the page into
    // the checklist of what is left; all ok shows what was set up.
    void setSteps(const QList<SetupStepStatus> &steps);

signals:
    // The user asked to go back to the step at this index of the last
    // setSteps() list.
    void stepSelected(int index);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void showBlockedSteps(const QList<SetupStepStatus> &steps);
    void showCompletedSteps(const QList<SetupStepStatus> &steps);

    ApplicationController &m_controller;
#ifdef Q_OS_LINUX
    void updateLinuxShortcutInstruction();
    QLabel *m_manualCommand = nullptr;
    QLabel *m_trayNote = nullptr;
#endif
    QLabel *m_intro;
    QLabel *m_shortcutStatus;
    QLabel *m_signInNote;
    // Everything that only belongs to one of the two states, so switching is a
    // matter of showing one container and hiding the other.
    QWidget *m_completed;
    QWidget *m_blocked;
    QWidget *m_completedList = nullptr;
    QWidget *m_blockedList = nullptr;
};

} // namespace speecher
