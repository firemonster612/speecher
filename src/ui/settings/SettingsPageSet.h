#pragma once

#include <QObject>

class QScrollArea;

namespace speecher {

class ApplicationController;
class ApplicationSettingsPage;
class AudioSettingsPage;
class BindingsSettingsPage;
class CorrectionsSettingsPage;
class GeneralSettingsPage;
class OutputSettingsPage;
class ProviderSettingsPage;
class RefinementSettingsPage;
class VocabularySettingsPage;

class SettingsPageSet : public QObject {
    Q_OBJECT

public:
    enum class SaveFailure {
        None,
        InvalidReplacementRules,
        DuplicatePasteRuleIds,
        ProviderSecret,
    };

    SettingsPageSet(ApplicationController *controller, QWidget *parent);

    GeneralSettingsPage *general() const;
    AudioSettingsPage *audio() const;
    ApplicationSettingsPage *applications() const;
    OutputSettingsPage *output() const;
    RefinementSettingsPage *refinement() const;
    ProviderSettingsPage *providers() const;
    VocabularySettingsPage *vocabulary() const;
    CorrectionsSettingsPage *corrections() const;
    BindingsSettingsPage *bindings() const;

    void load();
    void loadBeforeShow();
    void loadAfterShow();
    bool save(bool showValidationErrors = true,
              bool refreshPages = true,
              SaveFailure *failure = nullptr);
    bool hasChanges() const;
    void preserveBindingScroll(QScrollArea *scroll);

signals:
    void changed();

private:
    void updateAccessibilityState(bool supported, bool enabled, bool persistent);

    ApplicationController *m_controller;
    GeneralSettingsPage *m_general;
    AudioSettingsPage *m_audio;
    ApplicationSettingsPage *m_applications;
    OutputSettingsPage *m_output;
    RefinementSettingsPage *m_refinement;
    ProviderSettingsPage *m_providers;
    VocabularySettingsPage *m_vocabulary;
    CorrectionsSettingsPage *m_corrections;
    BindingsSettingsPage *m_bindings;
};

} // namespace speecher
