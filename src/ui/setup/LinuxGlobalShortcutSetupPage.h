#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QGroupBox;

namespace speecher {

class ApplicationController;

class LinuxGlobalShortcutSetupPage final : public QWidget {
public:
    explicit LinuxGlobalShortcutSetupPage(ApplicationController &controller,
                                          QWidget *parent = nullptr);

private:
    void installIntegration();
    void registerShortcut();
    void updateIntegrationState();
    void updateInstructionCommand();
    void showRegistrationResult(bool bound, const QString &detail);

    ApplicationController &m_controller;
    QString m_homePath;
    QString m_appImagePath;
    QString m_binaryPath;
    bool m_plasma = false;
    QGroupBox *m_integrationGroup = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
    QPushButton *m_registerButton = nullptr;
    QLabel *m_registrationStatus = nullptr;
    QGroupBox *m_manualGroup = nullptr;
    QLabel *m_command = nullptr;
};

} // namespace speecher
