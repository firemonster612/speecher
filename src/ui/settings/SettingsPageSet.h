#pragma once

#include "core/settings/SettingsSchema.h"
#include "frontend/qt/OutputCustomRows.h"

#include <QObject>
#include <QStringList>

class QScrollArea;

namespace speecher {

class ApplicationController;
class BindingsSettingsPage;
class CorrectionsSettingsPage;
class ProviderSettingsPage;
class SchemaSettingsPage;
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

    // The messages come from whichever validator refused, so a caller can show
    // what actually went wrong instead of re-narrating the enum.
    struct SaveOutcome {
        SaveFailure failure = SaveFailure::None;
        QStringList messages;
    };

    SettingsPageSet(ApplicationController *controller, QWidget *parent);

    SchemaSettingsPage *general() const;
    SchemaSettingsPage *audio() const;
    SchemaSettingsPage *applications() const;
    SchemaSettingsPage *output() const;
    SchemaSettingsPage *refinement() const;
    ProviderSettingsPage *providers() const;
    VocabularySettingsPage *vocabulary() const;
    CorrectionsSettingsPage *corrections() const;
    BindingsSettingsPage *bindings() const;

    void load();
    void loadBeforeShow();
    void loadAfterShow();
    bool save(bool showValidationErrors = true,
              bool refreshPages = true,
              SaveOutcome *outcome = nullptr);
    bool hasChanges() const;
    void preserveBindingScroll(QScrollArea *scroll);

signals:
    void changed();

private:
    void updateAccessibilityState(bool supported, bool enabled, bool persistent);
    void runPageAction(const QString &rowId);

    ApplicationController *m_controller;
    SettingsSchema m_schema;
    OutputCustomRows m_outputRows;
    SchemaSettingsPage *m_general;
    SchemaSettingsPage *m_audio;
    SchemaSettingsPage *m_applications;
    SchemaSettingsPage *m_output;
    SchemaSettingsPage *m_refinement;
    ProviderSettingsPage *m_providers;
    VocabularySettingsPage *m_vocabulary;
    CorrectionsSettingsPage *m_corrections;
    BindingsSettingsPage *m_bindings;
};

} // namespace speecher
