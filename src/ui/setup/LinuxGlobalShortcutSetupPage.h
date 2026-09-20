#pragma once

#include "core/ShortcutBinding.h"

#include <QKeySequence>
#include <QPushButton>
#include <QSet>
#include <QWidget>

#include <optional>

class QLabel;
class QComboBox;
class QProgressBar;
class QShowEvent;

namespace speecher {

class ApplicationController;

// Records the dictation shortcut as one button: click to arm, press a key
// combination or a single key, and it applies immediately. A combination
// commits as soon as its non-modifier arrives; a bare modifier — which
// QKeySequenceEdit cannot capture — commits on release, once it is clear no
// other key is joining it, resolved to a KeyboardEvent.code from its evdev
// scan code. One control replaces the earlier key-combination and single-key
// button pair, which presented one shortcut as two different settings.
class ShortcutCaptureButton final : public QPushButton {
    Q_OBJECT

public:
    explicit ShortcutCaptureButton(QWidget *parent = nullptr);
    // The currently bound shortcut, shown while idle; empty shows
    // "Set shortcut".
    void setShortcutDisplay(const QString &display);
    // Where the desktop registers combinations, a bare non-modifier such as
    // F13 binds as a plain QKeySequence through that service; without one it
    // can only bind as a watched single key.
    void setCombinationsAvailable(bool available);

signals:
    void bindingCaptured(const ShortcutBinding &binding);
    // While armed the bound shortcut must not fire dictation; the page
    // suspends the binder for the duration, as the mac and Windows recorders
    // do.
    void armedChanged(bool armed);
    // A key with no vocabulary row (a media key): the page says so inline.
    void unknownKeyPressed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void setArmed(bool armed);
    QString idleText() const;
    void commitSingleKey(quint32 nativeScanCode);

    bool m_armed = false;
    bool m_combinationsAvailable = true;
    QString m_display;
    // Modifiers currently held while armed, by native scan code. A lone entry
    // is the commit-on-release candidate; a second one voids the candidate (a
    // modifier-only chord is not a shortcut) until all are released again.
    QSet<quint32> m_heldModifiers;
    quint32 m_pendingModifier = 0;
};

QString linuxGlobalShortcutManualInstruction();
QString linuxGlobalShortcutCommand();
// The wizard's caveat that the shortcut needs a running Speecher. Only claims
// a tray icon where a tray exists: portal shortcut support and a
// StatusNotifier host are independent (stock GNOME has the former, not the
// latter).
QString linuxTrayShortcutNote(bool trayAvailable);
// Shown where push-to-talk cannot be honoured: a manual desktop shortcut runs
// the toggle command, and some backends report only the press. Saying so beats
// leaving "dictate only while the key is held" on offer as though it worked.
QString linuxHoldToTalkUnavailableNote();

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
    void applyBinding(const ShortcutBinding &binding);
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
    QWidget *m_captureControls = nullptr;
    QWidget *m_portalControls = nullptr;
    QWidget *m_manualControls = nullptr;
    QWidget *m_keyHelperControls = nullptr;
    ShortcutCaptureButton *m_setShortcut = nullptr;
    QPushButton *m_chooseShortcut = nullptr;
    QLabel *m_captureLead = nullptr;
    QLabel *m_captureFeedback = nullptr;
    QWidget *m_activationModeRow = nullptr;
    QComboBox *m_activationMode = nullptr;
    QPushButton *m_keyHelperButton = nullptr;
    QLabel *m_keyHelperStatus = nullptr;
    QProgressBar *m_keyHelperProgress = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_command = nullptr;
    QLabel *m_trayNote = nullptr;
    QLabel *m_holdUnavailableNote = nullptr;
    QString m_displayedShortcut;
    QWidget *m_integration = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
    bool m_integrationHidden = false;
    std::optional<bool> m_notifiedStepComplete;
};

} // namespace speecher
