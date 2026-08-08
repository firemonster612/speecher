#pragma once

#include <QDialog>

class QPushButton;

namespace speecher {

class ApplicationController;
class AccessibilityNotice;
class SettingsPageSet;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ApplicationController *controller, QWidget *parent = nullptr);

private:
    void load();
    bool save();
    void updateButtonState();
    void updateAccessibilityState(bool supported, bool enabled, bool persistent);

    ApplicationController *m_controller = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    AccessibilityNotice *m_accessibilityNotice = nullptr;
    int m_preservedScrollValue = 0;
    SettingsPageSet *m_pages = nullptr;
};

} // namespace speecher
