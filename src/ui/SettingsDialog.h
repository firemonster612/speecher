#pragma once

#include "core/AppSettings.h"

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
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
class ProviderSettingsPage;
class RefinementSettingsPage;
class VocabularySettingsPage;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ApplicationController *controller, QWidget *parent = nullptr);

private:
    void load();
    bool save();
    bool hasChanges() const;
    void refreshAudioDeviceList(const QString &selectedDeviceId);
    void updateAudioControls();
    void updateButtonState();

    ApplicationController *m_controller = nullptr;
    QComboBox *m_audioDevice = nullptr;
    QComboBox *m_captureMode = nullptr;
    QCheckBox *m_vadEnabled = nullptr;
    QLabel *m_runtimeStatus = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QScrollArea *m_scroll = nullptr;
    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;
    QSpinBox *m_preRollMs = nullptr;
    QSpinBox *m_postRollMs = nullptr;
    QSpinBox *m_readinessTimeoutMs = nullptr;
    QSpinBox *m_vadThreshold = nullptr;
    BindingsSettingsPage *m_bindingsPage = nullptr;
    GeneralSettingsPage *m_generalPage = nullptr;
    OutputSettingsPage *m_outputPage = nullptr;
    ProviderSettingsPage *m_providerPage = nullptr;
    RefinementSettingsPage *m_refinementPage = nullptr;
    VocabularySettingsPage *m_vocabularyPage = nullptr;
    CorrectionsSettingsPage *m_correctionsPage = nullptr;
};

} // namespace speecher
