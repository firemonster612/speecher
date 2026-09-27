#pragma once

#include "core/settings/SettingsSchema.h"
#include "frontend/qt/BindingRows.h"
#include "frontend/qt/OutputCustomRows.h"
#include "frontend/qt/ProviderCustomRows.h"

#include <QHash>
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

    const SettingsSchema &schema() const;
    // The page showing a pane's groups, by pane id, or by "pane:view" for one
    // view of an Alternatives pane. Null for a pane with no schema rows (Home,
    // Transcribe) and for one this build does not have.
    SchemaSettingsPage *page(const QString &id) const;

    void load();
    void loadBeforeShow();
    void loadAfterShow();
    bool save(bool showValidationErrors = true,
              bool refreshPages = true,
              SaveOutcome *outcome = nullptr);
    void prepareForSettingsDeletion();

signals:
    void changed();
    void settingsDeletionStarted();
    void whatsNewRequested();
    void localModelsRequested();

private:
    void addPage(const QString &id,
                 const QList<SettingsSection> &sections,
                 QWidget *parent,
                 const SchemaCustomRowFactory &customRows);
    // Keeps the replacements list where it was while its rows are rebuilt.
    void preserveScroll(QScrollArea *scroll);
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
    AppSettings m_loaded;
    OutputCustomRows m_outputRows;
    BindingRows m_bindingRows;
    ProviderCustomRows m_providerRows;
    QHash<QString, SchemaSettingsPage *> m_pages;
};

} // namespace speecher
