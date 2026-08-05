#pragma once

#include <QDialog>

class QPushButton;

namespace speecher {

class ApplicationController;
class AudioSettingsPage;
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
    void updateButtonState();

    ApplicationController *m_controller = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    AudioSettingsPage *m_audioPage = nullptr;
    BindingsSettingsPage *m_bindingsPage = nullptr;
    GeneralSettingsPage *m_generalPage = nullptr;
    OutputSettingsPage *m_outputPage = nullptr;
    ProviderSettingsPage *m_providerPage = nullptr;
    RefinementSettingsPage *m_refinementPage = nullptr;
    VocabularySettingsPage *m_vocabularyPage = nullptr;
    CorrectionsSettingsPage *m_correctionsPage = nullptr;
};

} // namespace speecher
