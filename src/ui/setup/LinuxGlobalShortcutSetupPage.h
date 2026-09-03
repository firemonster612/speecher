#pragma once

#include <QWidget>

class QLabel;
class QKeySequenceEdit;
class QPushButton;

namespace speecher {

class ApplicationController;

QString linuxGlobalShortcutManualInstruction();
QString linuxGlobalShortcutCommand();

class LinuxGlobalShortcutSetupPage final : public QWidget {
public:
    explicit LinuxGlobalShortcutSetupPage(ApplicationController &controller,
                                          QWidget *parent = nullptr);

private:
    void installIntegration();
    void setShortcut();
    void chooseShortcut();
    void refresh();
    void showRegistrationResult(const QString &detail);

    ApplicationController &m_controller;
    QString m_homePath;
    QString m_appImagePath;
    QString m_binaryPath;
    QWidget *m_keySequenceControls = nullptr;
    QWidget *m_portalControls = nullptr;
    QWidget *m_manualControls = nullptr;
    QKeySequenceEdit *m_sequence = nullptr;
    QPushButton *m_setShortcut = nullptr;
    QPushButton *m_chooseShortcut = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_command = nullptr;
    QWidget *m_integration = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
};

} // namespace speecher
