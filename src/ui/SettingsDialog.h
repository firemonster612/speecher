#pragma once

#include "core/AppSettings.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace speecher {

class ApplicationController;
class BindingsSettingsPage;
class CorrectionsSettingsPage;
class GeneralSettingsPage;
class OutputSettingsPage;
class VocabularySettingsPage;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ApplicationController *controller, QWidget *parent = nullptr);

private:
    void load();
    bool save();
    bool hasChanges() const;
    QList<WritingProfileSettings> currentWritingProfileSettings() const;
    QList<WritingProfileOverride> currentWritingProfileOverrides() const;
    void setWritingProfileSettings(const QList<WritingProfileSettings> &settings);
    void setWritingProfileOverrides(const QList<WritingProfileOverride> &overrides);
    void addWritingProfileOverride(const WritingProfileOverride &override = {});
    void refreshAudioDeviceList(const QString &selectedDeviceId);
    void updateAudioControls();
    void updateAuthControl();
    void updateAnthropicControls();
    void updateScreenshotControl();
    void showAnthropicAuthInfo();
    void updateButtonState();

    ApplicationController *m_controller = nullptr;
    QComboBox *m_audioDevice = nullptr;
    QComboBox *m_captureMode = nullptr;
    QCheckBox *m_vadEnabled = nullptr;
    QComboBox *m_provider = nullptr;
    QComboBox *m_writingProfile = nullptr;
    QCheckBox *m_useTargetContext = nullptr;
    QCheckBox *m_screenshotContext = nullptr;
    QComboBox *m_openAiModel = nullptr;
    QComboBox *m_openAiEffort = nullptr;
    QComboBox *m_anthropicModel = nullptr;
    QComboBox *m_anthropicEffort = nullptr;
    QComboBox *m_authMode = nullptr;
    QComboBox *m_anthropicAuthMode = nullptr;
    QStackedWidget *m_authControl = nullptr;
    QLabel *m_authStatus = nullptr;
    QLabel *m_anthropicWarning = nullptr;
    QWidget *m_anthropicWarningRow = nullptr;
    QLabel *m_runtimeStatus = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_anthropicInfoButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;
    QSpinBox *m_preRollMs = nullptr;
    QSpinBox *m_postRollMs = nullptr;
    QSpinBox *m_readinessTimeoutMs = nullptr;
    QSpinBox *m_vadThreshold = nullptr;
    QTableWidget *m_profileSettings = nullptr;
    QTableWidget *m_appProfileOverrides = nullptr;
    QPushButton *m_addAppProfileOverrideButton = nullptr;
    QPushButton *m_removeAppProfileOverrideButton = nullptr;
    BindingsSettingsPage *m_bindingsPage = nullptr;
    GeneralSettingsPage *m_generalPage = nullptr;
    OutputSettingsPage *m_outputPage = nullptr;
    VocabularySettingsPage *m_vocabularyPage = nullptr;
    CorrectionsSettingsPage *m_correctionsPage = nullptr;
};

} // namespace speecher
