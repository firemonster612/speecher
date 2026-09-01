#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QGroupBox;
class QToolButton;

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
    void updateRemovalGuide();
    void refreshRegistrationState();
    void showWorkingState(const QString &display);
    void showRegistrationResult(bool bound, const QString &detail);

    ApplicationController &m_controller;
    QString m_homePath;
    QString m_appImagePath;
    QString m_binaryPath;
    bool m_plasma = false;
    bool m_working = false;
    QWidget *m_options = nullptr;
    QLabel *m_confirmation = nullptr;
    QToolButton *m_moreOptions = nullptr;
    QGroupBox *m_integrationGroup = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
    QPushButton *m_registerButton = nullptr;
    QLabel *m_registrationStatus = nullptr;
    QGroupBox *m_manualGroup = nullptr;
    QLabel *m_command = nullptr;
    QLabel *m_removal = nullptr;
};

} // namespace speecher
