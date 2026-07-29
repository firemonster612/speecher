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

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ApplicationController *controller, QWidget *parent = nullptr);

private:
    void load();
    bool save();
    bool hasChanges() const;
    QStringList currentVocabulary() const;
    QList<BindingRule> currentBindingRules() const;
    QList<LearnedCorrection> currentLearnedCorrections() const;
    void setVocabularyRows(const QStringList &terms);
    void setBindingRules(const QList<BindingRule> &rules);
    void setLearnedCorrections(const QList<LearnedCorrection> &corrections);
    void refreshBindingList();
    void editBinding(int row);
    void refreshAudioDeviceList(const QString &selectedDeviceId);
    void updateAudioControls();
    void updateAuthControl();
    void updateAnthropicControls();
    void showAnthropicAuthInfo();
    void refreshOutputControls();
    void updateYdotoolButtons();
    void updateButtonState();
    void updateVocabularyLimit();
    void setupOrEnableYdotool();
    void disableYdotool();
    void removeYdotoolSetup();
    bool verifyYdotoolTyping();

    ApplicationController *m_controller = nullptr;
    QComboBox *m_theme = nullptr;
    QComboBox *m_audioDevice = nullptr;
    QComboBox *m_captureMode = nullptr;
    QCheckBox *m_pauseMedia = nullptr;
    QCheckBox *m_sounds = nullptr;
    QCheckBox *m_vadEnabled = nullptr;
    QComboBox *m_provider = nullptr;
    QComboBox *m_refinementStyle = nullptr;
    QComboBox *m_writingProfile = nullptr;
    QCheckBox *m_useTargetContext = nullptr;
    QCheckBox *m_screenshotContext = nullptr;
    QComboBox *m_openAiModel = nullptr;
    QComboBox *m_openAiEffort = nullptr;
    QComboBox *m_anthropicModel = nullptr;
    QComboBox *m_anthropicEffort = nullptr;
    QComboBox *m_outputMethod = nullptr;
    QComboBox *m_outputFormat = nullptr;
    QComboBox *m_globalPaste = nullptr;
    QComboBox *m_terminalPaste = nullptr;
    QCheckBox *m_restoreClipboardAfterTyping = nullptr;
    QCheckBox *m_learnCorrections = nullptr;
    QComboBox *m_authMode = nullptr;
    QComboBox *m_anthropicAuthMode = nullptr;
    QStackedWidget *m_authControl = nullptr;
    QLabel *m_authStatus = nullptr;
    QLabel *m_anthropicWarning = nullptr;
    QWidget *m_anthropicWarningRow = nullptr;
    QLabel *m_ydotoolStatus = nullptr;
    QLabel *m_vocabLimit = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_ydotoolSetupButton = nullptr;
    QPushButton *m_ydotoolStartButton = nullptr;
    QPushButton *m_ydotoolDisableButton = nullptr;
    QPushButton *m_ydotoolRemoveButton = nullptr;
    QPushButton *m_anthropicInfoButton = nullptr;
    QPushButton *m_addBindingButton = nullptr;
    QPushButton *m_openReleasesButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QSpinBox *m_previewWords = nullptr;
    QSpinBox *m_preRollMs = nullptr;
    QSpinBox *m_postRollMs = nullptr;
    QSpinBox *m_readinessTimeoutMs = nullptr;
    QSpinBox *m_vadThreshold = nullptr;
    QTableWidget *m_vocab = nullptr;
    QTableWidget *m_corrections = nullptr;
    QPushButton *m_removeCorrectionButton = nullptr;
    QPushButton *m_undoCorrectionButton = nullptr;
    QListWidget *m_bindings = nullptr;
    QList<LearnedCorrection> m_removedCorrections;
    QList<BindingRule> m_bindingRules;
    bool m_updatingVocabulary = false;
};

} // namespace speecher
