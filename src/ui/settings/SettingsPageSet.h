#pragma once

#include "core/settings/SettingsSchema.h"
#include "frontend/qt/BindingRows.h"
#include "frontend/qt/OutputCustomRows.h"
#include "frontend/qt/ProviderCustomRows.h"

#include <QObject>
#include <QStringList>

class QScrollArea;

namespace speecher {

class ApplicationController;
class SchemaSettingsPage;

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
    SettingsPageSet(ApplicationController *controller,
                    QWidget *parent,
                    SettingsSchema schema);

    // The schema's providers page split across the Models and Auth sidebar
    // sections. Exposed so tests can prove the two lists cover every row.
    static QStringList providerModelRowIds();
    static QStringList providerAuthRowIds();

    SchemaSettingsPage *general() const;
    SchemaSettingsPage *audio() const;
    SchemaSettingsPage *applications() const;
    SchemaSettingsPage *output() const;
    SchemaSettingsPage *refinement() const;
    SchemaSettingsPage *providerModels() const;
    SchemaSettingsPage *providerAuth() const;
    SchemaSettingsPage *vocabulary() const;
    SchemaSettingsPage *corrections() const;
    SchemaSettingsPage *bindings() const;
    SchemaSettingsPage *whatsNew() const;

    void load();
    void loadBeforeShow();
    void loadAfterShow();
    bool save(bool showValidationErrors = true,
              bool refreshPages = true,
              SaveOutcome *outcome = nullptr);
    void prepareForSettingsDeletion();
    void preserveBindingScroll(QScrollArea *scroll);

signals:
    void changed();
    void settingsDeletionStarted();
    void whatsNewRequested();

private:
    SchemaSettingsPage *addPage(const QString &id,
                                QWidget *parent,
                                SchemaCustomRowFactory customRows = {});
    SchemaSettingsPage *addPage(const SettingsPage &page,
                                QWidget *parent,
                                SchemaCustomRowFactory customRows = {});
    void updateAccessibilityState(bool supported, bool enabled, bool persistent);
    void applyCapabilities();
    void runPageAction(const QString &rowId);
#ifdef Q_OS_LINUX
    void removeSpeecher();
#endif
    void refreshUpdateRows();

    ApplicationController *m_controller;
    bool m_settingsDeletionStarted = false;
    bool m_targetAccessibility = false;
    SettingsSchema m_schema;
    AppSettings m_draft;
    OutputCustomRows m_outputRows;
    BindingRows m_bindingRows;
    ProviderCustomRows m_providerRows;
    QList<SchemaSettingsPage *> m_pages;
    SchemaSettingsPage *m_general;
    SchemaSettingsPage *m_audio;
    SchemaSettingsPage *m_applications;
    SchemaSettingsPage *m_output;
    SchemaSettingsPage *m_refinement;
    SchemaSettingsPage *m_vocabulary;
    SchemaSettingsPage *m_corrections;
    SchemaSettingsPage *m_bindings;
    SchemaSettingsPage *m_providerModels;
    SchemaSettingsPage *m_providerAuth;
    SchemaSettingsPage *m_whatsNew;
};

} // namespace speecher
