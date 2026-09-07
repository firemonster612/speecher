#pragma once

#include <QWidget>

#include <optional>

class QLabel;
class QKeySequenceEdit;
class QPushButton;
class QShowEvent;

namespace speecher {

class ApplicationController;

QString linuxGlobalShortcutManualInstruction();
QString linuxGlobalShortcutCommand();

class LinuxGlobalShortcutSetupPage final : public QWidget {
    Q_OBJECT

public:
    explicit LinuxGlobalShortcutSetupPage(ApplicationController &controller,
                                          QWidget *parent = nullptr);
    void hideAppMenuIntegration();

    // True while this AppImage run still needs the user to click Install
    // Speecher.
    bool installRequired() const;

    // The setup assistant holds Next until the install has run and, where the
    // desktop can register one, a Global Shortcut is set. Manual-command
    // desktops cannot be verified, so the install is their whole step.
    bool stepComplete() const;

signals:
    void stepCompleteChanged();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void installIntegration();
    void setShortcut();
    void chooseShortcut();
    void refresh();
    void refreshControls();
    void showRegistrationResult(bool bound, const QString &detail);

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
    QString m_displayedShortcut;
    QWidget *m_integration = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
    bool m_integrationHidden = false;
    std::optional<bool> m_notifiedStepComplete;
};

} // namespace speecher
