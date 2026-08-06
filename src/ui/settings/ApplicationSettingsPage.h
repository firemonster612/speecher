#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QPushButton;
class QTableWidget;

namespace speecher {

class ApplicationSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit ApplicationSettingsPage(QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;
    void setTargetAccessibilityAvailable(bool available);

signals:
    void changed();

private:
    void addBuiltInRule(const AppRecognitionRule &rule);
    void addCustomRule(const AppRecognitionRule &rule = {});
    QList<AppRecognitionRule> currentRules() const;
    void updateDeleteButton();

    QWidget *m_controls = nullptr;
    QTableWidget *m_rules = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    int m_builtInCount = 0;
};

} // namespace speecher
