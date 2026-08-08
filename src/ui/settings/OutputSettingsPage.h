#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace speecher {

class SettingsStore;

class OutputSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit OutputSettingsPage(SettingsStore &settings, QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    bool validate(bool showError = true) const;
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;
    void refreshControls();
    void setTargetAccessibilityAvailable(bool available);

signals:
    void changed();

private:
    QList<PasteRule> currentApplicationPasteRules() const;
    QList<PasteRule> currentCategoryPasteRules() const;
    void setApplicationPasteRules(const QList<PasteRule> &rules);
    void addApplicationPasteRule(const PasteRule &rule = {});
    void updateYdotoolButtons();
    void setupOrEnableYdotool();
    void disableYdotool();
    void removeYdotoolSetup();
    bool verifyYdotoolTyping();

    SettingsStore &m_settings;
    QComboBox *m_outputMethod;
    QComboBox *m_outputFormat;
    QComboBox *m_globalPaste;
    QList<QPair<AppCategory, QComboBox *>> m_categoryPasteControls;
    QCheckBox *m_restoreClipboardAfterTyping;
    QLabel *m_ydotoolStatus;
    QTableWidget *m_appPasteRules;
    QPushButton *m_addAppPasteRuleButton;
    QPushButton *m_removeAppPasteRuleButton;
    QPushButton *m_ydotoolSetupButton;
    QPushButton *m_ydotoolStartButton;
    QPushButton *m_ydotoolDisableButton;
    QPushButton *m_ydotoolRemoveButton;
    QWidget *m_targetPasteControls = nullptr;
};

} // namespace speecher
