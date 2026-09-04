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

// The schema's pages as Qt widgets, one per sidebar entry after Dictation,
// plus What's New. Owns the draft they all edit and the save that commits it.
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

    SchemaSettingsPage *general() const;
    SchemaSettingsPage *audio() const;
    SchemaSettingsPage *output() const;
    SchemaSettingsPage *accounts() const;
    SchemaSettingsPage *refinement() const;
    SchemaSettingsPage *vocabulary() const;
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
    SchemaSettingsPage *m_output;
    SchemaSettingsPage *m_accounts;
    SchemaSettingsPage *m_refinement;
    SchemaSettingsPage *m_vocabulary;
    SchemaSettingsPage *m_whatsNew;
};

} // namespace speecher
