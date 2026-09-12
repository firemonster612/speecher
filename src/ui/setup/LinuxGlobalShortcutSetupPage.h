#pragma once

#include "core/ShortcutBinding.h"

#include <QKeySequence>
#include <QPushButton>
#include <QWidget>

#include <optional>

class QLabel;
class QComboBox;
class QProgressBar;
class QShowEvent;

namespace speecher {

class ApplicationController;

// Records one physical key, a bare modifier included, which QKeySequenceEdit
// cannot capture. While armed it takes focus and reports the first key pressed
// as a KeyboardEvent.code, resolved from the key's evdev scan code.
class SingleKeyCaptureButton final : public QPushButton {
    Q_OBJECT

public:
    explicit SingleKeyCaptureButton(QWidget *parent = nullptr);

signals:
    void keyCaptured(const ShortcutBinding &binding);
    // While armed the bound key must not fire dictation; the page suspends
    // the binder for the duration, as the mac and Windows recorders do.
    void armedChanged(bool armed);
    // A key with no vocabulary row (a media key): the page says so inline.
    void unknownKeyPressed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void setArmed(bool armed);

    bool m_armed = false;
};

// Records a key combination as one button: click to arm, press the shortcut,
// and it applies immediately. Replaces the QKeySequenceEdit-plus-apply-button
// pair, whose separate "Set shortcut" step users missed.
class ShortcutCaptureButton final : public QPushButton {
    Q_OBJECT

public:
    explicit ShortcutCaptureButton(QWidget *parent = nullptr);
    // The currently bound combination, shown while idle; empty shows
    // "Set shortcut".
    void setShortcutDisplay(const QString &display);

signals:
    void sequenceCaptured(const QKeySequence &sequence);
    // While armed the bound shortcut must not fire dictation; the page
    // suspends the binder for the duration, as the single-key recorder does.
    void armedChanged(bool armed);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void setArmed(bool armed);
    QString idleText() const;

    bool m_armed = false;
    QString m_display;
};

QString linuxGlobalShortcutManualInstruction();
QString linuxGlobalShortcutCommand();
// The wizard's caveat that the shortcut needs a running Speecher. Only claims
// a tray icon where a tray exists: portal shortcut support and a
// StatusNotifier host are independent (stock GNOME has the former, not the
// latter).
QString linuxTrayShortcutNote(bool trayAvailable);

class LinuxGlobalShortcutSetupPage final : public QWidget {
    Q_OBJECT

public:
    explicit LinuxGlobalShortcutSetupPage(ApplicationController &controller,
                                          QWidget *parent = nullptr);
    void hideAppMenuIntegration();
    // The General settings page already renders the activation-mode schema
    // row, so the embedded copy hides its own combo to avoid two controls over
    // one setting. The wizard step keeps it.
    void hideActivationMode();

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
    void applyShortcut(const QKeySequence &sequence);
    void saveSingleKey(const ShortcutBinding &binding);
    void chooseShortcut();
    void installKeyHelper();
    void refresh();
    void refreshControls();
    void refreshKeyHelper();
    void showRegistrationResult(bool bound, const QString &detail);

    ApplicationController &m_controller;
    QString m_homePath;
    QString m_appImagePath;
    QString m_binaryPath;
    bool m_waylandSession = false;
    QWidget *m_keySequenceControls = nullptr;
    QWidget *m_portalControls = nullptr;
    QWidget *m_manualControls = nullptr;
    QWidget *m_singleKeyControls = nullptr;
    QWidget *m_keyHelperControls = nullptr;
    ShortcutCaptureButton *m_setShortcut = nullptr;
    QPushButton *m_chooseShortcut = nullptr;
    SingleKeyCaptureButton *m_captureKey = nullptr;
    QLabel *m_singleKeyLead = nullptr;
    QLabel *m_singleKeyWarning = nullptr;
    QWidget *m_activationModeRow = nullptr;
    QComboBox *m_activationMode = nullptr;
    QPushButton *m_keyHelperButton = nullptr;
    QLabel *m_keyHelperStatus = nullptr;
    QProgressBar *m_keyHelperProgress = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_command = nullptr;
    QLabel *m_trayNote = nullptr;
    QString m_displayedShortcut;
    QWidget *m_integration = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
    bool m_integrationHidden = false;
    std::optional<bool> m_notifiedStepComplete;
};

} // namespace speecher
