#pragma once

#include "core/ShortcutBinding.h"
#include "ui/setup/SetupPages.h"

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
struct SettingsRow;

// Records the dictation shortcut as one button: click to arm, press a key
// combination or a single key, and it applies immediately; click again or
// press Escape to give up. A combination
// commits as soon as its non-modifier arrives; a bare modifier — which
// QKeySequenceEdit cannot capture — commits on release, once it is clear no
// other key is joining it, resolved to a KeyboardEvent.code from its evdev
// scan code. One control replaces the earlier key-combination and single-key
// button pair, which presented one shortcut as two different settings.
class ShortcutCaptureButton final : public QPushButton {
    Q_OBJECT

public:
    explicit ShortcutCaptureButton(QWidget *parent = nullptr);
    // The currently bound shortcut, which the row shows beside this button.
    // Empty reads "Set shortcut", anything else "Change…".
    void setShortcutDisplay(const QString &display);
    bool armed() const { return m_armed; }
    // Where the desktop registers combinations, a bare non-modifier such as
    // F13 binds as a plain QKeySequence through that service; without one it
    // can only bind as a watched single key.
    void setCombinationsAvailable(bool available);
    // Escape gives up the capture unless it can be the shortcut, where the
    // button's own Cancel caption gives it up instead.
    void setEscapeRecords(bool records) { m_escapeRecords = records; }

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
    bool m_escapeRecords = false;
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

class LinuxGlobalShortcutSetupPage final : public QWidget, public SetupStep {
    Q_OBJECT

public:
    enum class Placement {
        // The setup step: the install, the shortcut's card, and the
        // activation mode.
        SetupStep,
        // The Dictation settings card's rows: the page already renders the
        // activation-mode row, and Speecher is installed by then.
        SettingsCard,
    };

    // The Cancel Shortcut's row is a settings card row with only the
    // recorder: no default to reset to, no helper install and no manual
    // command, which the dictation row beside it already offers.
    explicit LinuxGlobalShortcutSetupPage(ApplicationController &controller,
                                          QWidget *parent = nullptr,
                                          Placement placement = Placement::SetupStep,
                                          GlobalShortcutRole role = GlobalShortcutRole::Dictation);

    // True while this AppImage run still needs the user to click Install
    // Speecher.
    bool installRequired() const;

    // The setup assistant holds Next until the install has run and, where the
    // desktop can register one, a Global Shortcut is set. Manual-command
    // desktops cannot be verified, so the install is their whole step; holding
    // Finish there would leave setup impossible to finish, and the page
    // already shows the command to bind by hand.
    bool stepComplete() const;

    QString blockedReason() const override;

signals:
    void stepCompleteChanged();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void installIntegration();
    void applyBinding(const ShortcutBinding &binding);
    void resetShortcut();
    void clearShortcut();
    void chooseShortcut();
    void installKeyHelper();
    void refresh();
    void refreshControls();
    void refreshKeyHelper();
    // The feedback line only occupies the card while it has something to say.
    void showCaptureFeedback(const QString &text);
    void setStatus(const QString &text);
    // The row's description: what to press while capturing, else the latest
    // status, else what the shortcut is for.
    void refreshDescription();
    void showRegistrationResult(bool bound, const QString &detail);
    const SettingsRow &shortcutRow() const;

    ApplicationController &m_controller;
    GlobalShortcutRole m_role = GlobalShortcutRole::Dictation;
    QString m_homePath;
    QString m_appImagePath;
    QString m_binaryPath;
    bool m_waylandSession = false;
    QWidget *m_captureControls = nullptr;
    QWidget *m_manualControls = nullptr;
    QWidget *m_keyHelperControls = nullptr;
    ShortcutCaptureButton *m_setShortcut = nullptr;
    QPushButton *m_chooseShortcut = nullptr;
    QPushButton *m_resetShortcut = nullptr;
    QPushButton *m_clearShortcut = nullptr;
    QLabel *m_binding = nullptr;
    QLabel *m_description = nullptr;
    QString m_statusText;
    bool m_combinationsAvailable = false;
    QLabel *m_captureFeedback = nullptr;
    QWidget *m_activationModeRow = nullptr;
    QComboBox *m_activationMode = nullptr;
    QPushButton *m_keyHelperButton = nullptr;
    QLabel *m_keyHelperStatus = nullptr;
    QProgressBar *m_keyHelperProgress = nullptr;
    QLabel *m_command = nullptr;
    QLabel *m_trayNote = nullptr;
    QLabel *m_holdUnavailableNote = nullptr;
    QString m_displayedShortcut;
    QWidget *m_integration = nullptr;
    // The setup step's Cancel and Pause Shortcuts.
    QWidget *m_sessionShortcuts = nullptr;
    QPushButton *m_integrationButton = nullptr;
    QLabel *m_integrationStatus = nullptr;
    bool m_settingsCard = false;
    std::optional<bool> m_notifiedStepComplete;
};

} // namespace speecher
