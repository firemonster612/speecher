#include "ui/setup/LinuxGlobalShortcutSetupPage.h"

#include "app/ApplicationController.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "platform/KeywatchSetup.h"
#include "platform/LinuxDesktopIntegration.h"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

namespace speecher {
namespace {

// X11 and xkb report a physical key as its evdev code plus eight.
constexpr int x11KeycodeOffset = 8;

QLabel *guidanceLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

QString shortcutSetStatus(const QString &display)
{
    return QStringLiteral("Shortcut set to %1. Try it now.").arg(display);
}

// The keys QKeySequenceEdit also waits through: a chord is only complete once
// a non-modifier arrives.
bool isModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_Mode_switch:
    case Qt::Key_unknown:
        return true;
    default:
        return false;
    }
}

// One lead for the one capture control. Where the desktop registers
// combinations the whole range is on offer; portal desktops pick combinations
// through their own dialog ("Or press…" reads on from that block), and
// manual-command desktops can only watch a single key.
QString captureLead(bool combinationsAvailable, bool followsPortal)
{
    if (combinationsAvailable) {
        return QStringLiteral(
            "Press a key combination, or a single key such as Right Alt or F13.");
    }
    return followsPortal
        ? QStringLiteral("Or press a single key, such as Right Alt or F13, to use on its own.")
        : QStringLiteral("Press a single key, such as Right Alt or F13, to use on its own.");
}

} // namespace

ShortcutCaptureButton::ShortcutCaptureButton(QWidget *parent)
    : QPushButton(QStringLiteral("Set shortcut"), parent)
{
    setCheckable(true);
    connect(this, &QPushButton::clicked, this, [this](bool checked) { setArmed(checked); });
}

QString ShortcutCaptureButton::idleText() const
{
    return m_display.isEmpty() ? QStringLiteral("Set shortcut") : m_display;
}

void ShortcutCaptureButton::setShortcutDisplay(const QString &display)
{
    m_display = display;
    if (!m_armed) {
        setText(idleText());
    }
}

void ShortcutCaptureButton::setCombinationsAvailable(bool available)
{
    m_combinationsAvailable = available;
}

void ShortcutCaptureButton::setArmed(bool armed)
{
    if (m_armed == armed) {
        setChecked(armed);
        return;
    }
    m_armed = armed;
    setChecked(armed);
    m_heldModifiers.clear();
    m_pendingModifier = 0;
    setText(armed ? QStringLiteral("Press a key or key combination…") : idleText());
    if (armed) {
        setFocus(Qt::OtherFocusReason);
    }
    emit armedChanged(armed);
}

void ShortcutCaptureButton::commitSingleKey(quint32 nativeScanCode)
{
    if (const PhysicalKey *key = physicalKeyForEvdev(int(nativeScanCode) - x11KeycodeOffset)) {
        setArmed(false);
        emit bindingCaptured(ShortcutBinding::singleKey(QString::fromLatin1(key->code)));
        return;
    }
    // A key with no vocabulary row (e.g. a media key) stays armed; the page
    // says why rather than nothing happening.
    emit unknownKeyPressed();
}

void ShortcutCaptureButton::keyPressEvent(QKeyEvent *event)
{
    if (!m_armed || event->isAutoRepeat()) {
        QPushButton::keyPressEvent(event);
        return;
    }
    // Escape abandons the capture rather than becoming the shortcut, like the
    // mac and Windows recorders.
    if (event->key() == Qt::Key_Escape) {
        setArmed(false);
        return;
    }
    if (isModifierKey(event->key())) {
        m_heldModifiers.insert(event->nativeScanCode());
        m_pendingModifier =
            m_heldModifiers.size() == 1 ? event->nativeScanCode() : 0;
        event->accept();
        return;
    }
    m_pendingModifier = 0;
    if (event->modifiers() != Qt::NoModifier || m_combinationsAvailable) {
        const QKeySequence sequence(QKeyCombination(event->modifiers(), Qt::Key(event->key())));
        setArmed(false);
        emit bindingCaptured(ShortcutBinding(sequence));
        return;
    }
    commitSingleKey(event->nativeScanCode());
    event->accept();
}

void ShortcutCaptureButton::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_armed || event->isAutoRepeat() || !isModifierKey(event->key())) {
        QPushButton::keyReleaseEvent(event);
        return;
    }
    const quint32 scanCode = event->nativeScanCode();
    // Only a modifier whose press was seen counts; one already held when the
    // capture armed passes by without voiding a pending commit.
    if (!m_heldModifiers.remove(scanCode)) {
        event->accept();
        return;
    }
    const bool commit = m_pendingModifier != 0 && scanCode == m_pendingModifier;
    m_pendingModifier = 0;
    if (commit) {
        commitSingleKey(scanCode);
    }
    event->accept();
}

void ShortcutCaptureButton::focusOutEvent(QFocusEvent *event)
{
    if (m_armed) {
        setArmed(false);
    }
    QPushButton::focusOutEvent(event);
}

QString linuxGlobalShortcutManualInstruction()
{
    return QStringLiteral(
        "Speecher can't register a shortcut on this desktop. In your desktop's keyboard "
        "settings, add a shortcut that runs this command:");
}

QString linuxTrayShortcutNote(bool trayAvailable)
{
    if (trayAvailable) {
        return QStringLiteral(
            "The shortcut works while Speecher is running. Its icon in the "
            "system tray shows that it is ready.");
    }
    return QStringLiteral("The shortcut works while Speecher is running.");
}

QString linuxHoldToTalkUnavailableNote()
{
    return QStringLiteral(
        "With this shortcut, a tap starts and a second tap stops — hold-to-talk "
        "isn't available.");
}

QString linuxGlobalShortcutCommand()
{
    const QString homePath = QDir::homePath();
    QString appImagePath = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (!appImagePath.isEmpty()) {
        appImagePath = resolvedPath(appImagePath);
    }
    return globalShortcutInstructionCommand(
        homePath,
        appImagePath,
        resolvedPath(QCoreApplication::applicationFilePath()));
}

LinuxGlobalShortcutSetupPage::LinuxGlobalShortcutSetupPage(
    ApplicationController &controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_homePath(QDir::homePath())
    , m_appImagePath(QString::fromLocal8Bit(qgetenv("APPIMAGE")))
    , m_binaryPath(resolvedPath(QCoreApplication::applicationFilePath()))
    , m_waylandSession(isWaylandSession())
{
    if (!m_appImagePath.isEmpty()) {
        m_appImagePath = resolvedPath(m_appImagePath);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // Installing comes first, and the shortcut controls stay hidden until it
    // has happened: a shortcut bound to the pre-install path would break the
    // moment the install moves the image.
    m_integration = new QWidget(this);
    m_integration->setObjectName(QStringLiteral("appMenuIntegration"));
    auto *integrationLayout = new QVBoxLayout(m_integration);
    integrationLayout->setContentsMargins(0, 0, 0, 0);
    QString installFolder = appImageInstallDirectory(m_homePath);
    if (installFolder.startsWith(m_homePath)) {
        installFolder = QStringLiteral("~") + installFolder.mid(m_homePath.size());
    }
    integrationLayout->addWidget(guidanceLabel(
        QStringLiteral("Installing moves the Speecher AppImage to %1, adds it to your app "
                       "menu, and makes the speecher command available for desktop "
                       "shortcuts. Setup continues once Speecher is installed.")
            .arg(installFolder),
        m_integration));
    auto *integrationRow = new QHBoxLayout;
    m_integrationButton = new QPushButton(
        QStringLiteral("Install Speecher"), m_integration);
    m_integrationStatus = new QLabel(m_integration);
    integrationRow->addWidget(m_integrationButton);
    integrationRow->addWidget(m_integrationStatus, 1);
    integrationLayout->addLayout(integrationRow);
    m_integration->setVisible(!m_appImagePath.isEmpty());
    layout->addWidget(m_integration);

    // Portal desktops pick combinations through their own dialog, so their
    // block reads first and the capture lead below continues it with "Or
    // press a single key…".
    m_portalControls = new QWidget(this);
    m_portalControls->setObjectName(QStringLiteral("portalShortcut"));
    auto *portalLayout = new QVBoxLayout(m_portalControls);
    portalLayout->setContentsMargins(0, 0, 0, 0);
    portalLayout->addWidget(guidanceLabel(
        QStringLiteral("Your desktop will ask you to pick a key combination."),
        m_portalControls));
    m_chooseShortcut = new QPushButton(QStringLiteral("Choose shortcut"), m_portalControls);
    portalLayout->addWidget(m_chooseShortcut, 0, Qt::AlignLeft);
    layout->addWidget(m_portalControls);

    // One capture button records the whole range: a key combination or a
    // single key, a bare modifier included, which QKeySequenceEdit cannot
    // report. Any key records and saves; a warning explains the cost of a
    // typing key rather than a modal blocking it.
    m_captureControls = new QWidget(this);
    m_captureControls->setObjectName(QStringLiteral("shortcutCapture"));
    auto *captureLayout = new QVBoxLayout(m_captureControls);
    captureLayout->setContentsMargins(0, 0, 0, 0);
    // Reworded by refreshControls() for portal and manual desktops. Give it
    // the full wording now rather than starting empty: an empty word-wrap
    // label is allocated a collapsed height, and the button directly below
    // would paint over it on first show before the text-set relayout catches
    // up.
    m_captureLead = guidanceLabel(captureLead(true, false), m_captureControls);
    captureLayout->addWidget(m_captureLead);
    m_setShortcut = new ShortcutCaptureButton(m_captureControls);
    m_setShortcut->setObjectName(QStringLiteral("globalShortcutCapture"));
    captureLayout->addWidget(m_setShortcut, 0, Qt::AlignLeft);
    m_captureFeedback = guidanceLabel(QString(), m_captureControls);
    m_captureFeedback->setObjectName(QStringLiteral("shortcutCaptureFeedback"));
    captureLayout->addWidget(m_captureFeedback);

    // Wayland's only route to a single key is the privileged key-watch
    // helper. Recording stays enabled without it — combinations need no
    // helper — and a single key recorded too early is refused with the
    // helper's own status, which reads directly above this block's install
    // button.
    m_keyHelperControls = new QWidget(m_captureControls);
    m_keyHelperControls->setObjectName(QStringLiteral("keyHelperInstall"));
    auto *keyHelperLayout = new QVBoxLayout(m_keyHelperControls);
    keyHelperLayout->setContentsMargins(0, 0, 0, 0);
    keyHelperLayout->addWidget(guidanceLabel(
        QStringLiteral("On Wayland, a single-key shortcut needs a small helper that watches for "
                       "that one key. Setting it up asks for administrator permission once; "
                       "Speecher itself stays unprivileged. The helper only allows keys that "
                       "cannot type text: modifiers, Caps Lock and F13 to F24."),
        m_keyHelperControls));
    m_keyHelperButton = new QPushButton(QStringLiteral("Set up single-key helper"), m_keyHelperControls);
    keyHelperLayout->addWidget(m_keyHelperButton, 0, Qt::AlignLeft);
    m_keyHelperStatus = new QLabel(m_keyHelperControls);
    m_keyHelperStatus->setWordWrap(true);
    keyHelperLayout->addWidget(m_keyHelperStatus);
    m_keyHelperProgress = new QProgressBar(m_keyHelperControls);
    m_keyHelperProgress->setRange(0, 0);
    m_keyHelperProgress->setVisible(false);
    keyHelperLayout->addWidget(m_keyHelperProgress);
    captureLayout->addWidget(m_keyHelperControls);
    layout->addWidget(m_captureControls);

    m_status = guidanceLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("globalShortcutStatus"));
    layout->addWidget(m_status);

    m_manualControls = new QWidget(this);
    m_manualControls->setObjectName(QStringLiteral("manualShortcut"));
    auto *manualLayout = new QVBoxLayout(m_manualControls);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->addWidget(guidanceLabel(linuxGlobalShortcutManualInstruction(),
                                          m_manualControls));
    auto *commandRow = new QHBoxLayout;
    m_command = new QLabel(m_manualControls);
    m_command->setObjectName(QStringLiteral("globalShortcutCommand"));
    m_command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_command->setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::TextSelectableByKeyboard);
    auto *copy = new QToolButton(m_manualControls);
    copy->setText(QStringLiteral("Copy"));
    copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    commandRow->addWidget(m_command, 1);
    commandRow->addWidget(copy);
    manualLayout->addLayout(commandRow);
    layout->addWidget(m_manualControls);

    // Not shown on manual-command desktops: their command starts Speecher by
    // itself, so "only while running" would be wrong there.
    m_trayNote = guidanceLabel(QString(), this);
    m_trayNote->setObjectName(QStringLiteral("globalShortcutTrayNote"));
    layout->addWidget(m_trayNote);

    // The shortcut and its behaviour are set together. This is a native combo
    // bound to the same shortcuts/activationMode setting the General page's
    // schema row edits; the wizard page cannot host a SchemaSettingsPage row
    // (that renders a whole settings pane), so it shares the setting rather
    // than keeping a second copy of the value.
    m_activationModeRow = new QWidget(this);
    auto *modeRow = m_activationModeRow;
    auto *modeLayout = new QVBoxLayout(modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->addWidget(guidanceLabel(QStringLiteral("Shortcut behaviour"), modeRow));
    m_activationMode = new QComboBox(modeRow);
    m_activationMode->setObjectName(QStringLiteral("activationMode"));
    const auto addMode = [this](ShortcutActivationMode mode, const QString &label) {
        m_activationMode->addItem(label, shortcutActivationModeName(mode));
    };
    // The wording is the activationMode schema row's, so the wizard and the
    // General page describe each mode identically.
    addMode(ShortcutActivationMode::PushToTalk,
            QStringLiteral("Push to talk — dictate only while the key is held"));
    addMode(ShortcutActivationMode::Toggle,
            QStringLiteral("Toggle — one press starts, the next press stops"));
    addMode(ShortcutActivationMode::Hybrid,
            QStringLiteral("Hybrid — a tap toggles; holding dictates until release"));
    modeLayout->addWidget(m_activationMode, 0, Qt::AlignLeft);
    // Reads under the mode picker, where push-to-talk is on offer, because that
    // is the promise it corrects.
    m_holdUnavailableNote = guidanceLabel(linuxHoldToTalkUnavailableNote(), modeRow);
    m_holdUnavailableNote->setObjectName(QStringLiteral("holdToTalkUnavailable"));
    modeLayout->addWidget(m_holdUnavailableNote);
    layout->addWidget(modeRow);

    layout->addStretch();

    const auto currentMode = shortcutActivationModeName(
        m_controller.settings()->shortcutActivationMode());
    m_activationMode->setCurrentIndex(m_activationMode->findData(currentMode));
    connect(m_activationMode, &QComboBox::currentIndexChanged, this, [this] {
        m_controller.settings()->setShortcutActivationMode(
            shortcutActivationModeFromName(m_activationMode->currentData().toString()));
    });
    connect(m_keyHelperButton, &QPushButton::clicked, this, [this] { installKeyHelper(); });
    // The daemon's answer arrives after the probe that asked for it.
    connect(KeywatchSetup::daemonAnswer(), &KeywatchDaemonAnswer::changed, this, [this] {
        if (!m_keyHelperProgress->isVisible()) {
            refreshKeyHelper();
        }
    });

    connect(m_setShortcut, &ShortcutCaptureButton::bindingCaptured, this,
            [this](const ShortcutBinding &binding) { applyBinding(binding); });
    // While the capture is armed, the currently bound shortcut must record,
    // not fire dictation; the mac and Windows recorders suspend the same way.
    connect(m_setShortcut, &ShortcutCaptureButton::armedChanged, this, [this](bool armed) {
        if (armed) {
            // A stale refusal from the last attempt would read as a verdict on
            // the capture that is only just starting.
            m_captureFeedback->clear();
            m_controller.suspendGlobalShortcut();
        } else {
            m_controller.resumeGlobalShortcut();
        }
    });
    // The feedback label, not m_status: the status line is hidden on desktops
    // with no shortcut service, where a single key can still be recorded.
    connect(m_setShortcut, &ShortcutCaptureButton::unknownKeyPressed, this, [this] {
        m_captureFeedback->setText(QStringLiteral("That key cannot be a dictation key."));
    });
    connect(m_chooseShortcut, &QPushButton::clicked, this, [this] { chooseShortcut(); });
    connect(copy, &QToolButton::clicked, this, [this, copy] {
        QGuiApplication::clipboard()->setText(m_command->text().remove(QChar(0x200B)));
        copy->setIcon(QIcon::fromTheme(
            QStringLiteral("checkmark"),
            QIcon::fromTheme(QStringLiteral("dialog-ok-apply"))));
        QTimer::singleShot(1500, copy, [copy] {
            copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
        });
    });
    connect(m_integrationButton,
            &QPushButton::clicked,
            this,
            [this] { installIntegration(); });
    connect(&m_controller,
            &ApplicationController::globalShortcutChanged,
            this,
            [this] { refresh(); });
    connect(&m_controller,
            &ApplicationController::globalShortcutSupportChanged,
            this,
            [this] { refresh(); });
    connect(&m_controller,
            &ApplicationController::globalShortcutReleaseSupportChanged,
            this,
            [this] { refresh(); });
    connect(&m_controller,
            &ApplicationController::globalShortcutRegistrationFinished,
            this,
            [this](bool bound, const QString &detail) {
                showRegistrationResult(bound, detail);
            });

    refresh();
}

// The settings-embedded instance has no other trigger after the wizard's
// install moves the image: coming back to the page must not keep showing a
// manual command for the deleted path.
void LinuxGlobalShortcutSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void LinuxGlobalShortcutSetupPage::hideAppMenuIntegration()
{
    m_integration->hide();
    m_integrationHidden = true;
}

void LinuxGlobalShortcutSetupPage::hideActivationMode()
{
    m_activationModeRow->hide();
}

bool LinuxGlobalShortcutSetupPage::installRequired() const
{
    // A command link from an earlier version can point at an image still in
    // Downloads; that is not installed either — the move is part of the deal.
    return !m_integrationHidden && !m_appImagePath.isEmpty()
        && (!appImageIntegrationInstalled(m_homePath, m_appImagePath)
            || !appImageInInstallFolder(m_homePath, m_appImagePath));
}

bool LinuxGlobalShortcutSetupPage::stepComplete() const
{
    if (installRequired()) {
        return false;
    }
    if (!m_controller.globalShortcutSupportKnown()) {
        return false;
    }
    if (!m_controller.globalShortcutsSupported()) {
        return true;
    }
    return !m_controller.globalShortcutDisplay().isEmpty();
}

void LinuxGlobalShortcutSetupPage::installIntegration()
{
    QString error;
    QString installedPath;
    if (!relocateAppImage(m_homePath, m_appImagePath, &installedPath, &error)) {
        m_integrationStatus->setText(error);
        return;
    }
    if (installedPath != m_appImagePath) {
        // Everything that resolves the image path later (updates, restart,
        // shortcut commands) reads APPIMAGE, so the move has to land there.
        m_appImagePath = installedPath;
        qputenv("APPIMAGE", QFile::encodeName(installedPath));
    }
    if (!installAppImageIntegration(m_homePath,
                                    m_appImagePath,
                                    QCoreApplication::applicationDirPath(),
                                    &error)) {
        m_integrationStatus->setText(error);
        return;
    }
    m_integrationStatus->clear();
    refresh();
}

void LinuxGlobalShortcutSetupPage::applyBinding(const ShortcutBinding &binding)
{
    // A refusal is shown, never saved: a Wayland user asking for a letter is
    // told why rather than getting a binding that never fires. It reads in
    // the feedback label because m_status is hidden on desktops with no
    // shortcut service, where a single key can still be recorded.
    const QString reason = m_controller.globalShortcutUnsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        m_captureFeedback->setText(reason);
        return;
    }
    QString error;
    if (!m_controller.setGlobalShortcut(binding, &error)) {
        m_captureFeedback->setText(
            error.isEmpty() ? QStringLiteral("Couldn't set the shortcut.") : error);
        return;
    }
    m_captureFeedback->setText(binding.isSingleKey() ? singleKeyTypingWarning(binding)
                                                     : QString());
    m_setShortcut->setShortcutDisplay(m_controller.globalShortcut().displayText());
    m_status->setText(shortcutSetStatus(m_controller.globalShortcutDisplay()));
}

void LinuxGlobalShortcutSetupPage::installKeyHelper()
{
    m_keyHelperButton->setEnabled(false);
    m_keyHelperProgress->setVisible(true);
    m_keyHelperStatus->setText(QStringLiteral("Setting up the key helper…"));
    const auto error = std::make_shared<QString>();
    auto *thread = QThread::create([error] {
        KeywatchSetup::install(error.get());
    });
    const QPointer<LinuxGlobalShortcutSetupPage> guard(this);
    connect(thread, &QThread::finished, this, [this, guard, error] {
        if (!guard) {
            return;
        }
        m_keyHelperProgress->setVisible(false);
        refreshKeyHelper();
        // The reason the install failed beats the re-probed state it failed in.
        if (!error->isEmpty()) {
            m_keyHelperStatus->setText(*error);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void LinuxGlobalShortcutSetupPage::chooseShortcut()
{
    m_chooseShortcut->setEnabled(false);
    m_status->setText(QStringLiteral("Waiting for your desktop…"));
    m_controller.registerGlobalShortcut();
}

void LinuxGlobalShortcutSetupPage::refresh()
{
    refreshControls();
    // Only a real change may leave this widget: refresh() runs from
    // showEvent(), and an unconditional emit loops through the assistant's
    // gate update, whose button changes deliver new show events.
    const bool complete = stepComplete();
    if (m_notifiedStepComplete != complete) {
        m_notifiedStepComplete = complete;
        emit stepCompleteChanged();
    }
}

void LinuxGlobalShortcutSetupPage::refreshControls()
{
    // Another instance (the wizard's, next to this settings-embedded one) may
    // have moved the image and updated APPIMAGE since construction.
    const QString appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (!appImage.isEmpty()) {
        m_appImagePath = resolvedPath(appImage);
    }
    // Let the long path wrap at its separators inside a narrow card; the
    // zero-width spaces are stripped again when the command is copied.
    m_command->setWordWrap(true);
    m_command->setText(QString(globalShortcutInstructionCommand(
        m_homePath, m_appImagePath, m_binaryPath)).replace(QLatin1Char('/'), QStringLiteral("/\u200B")));
    if (!m_appImagePath.isEmpty()) {
        const bool installed = appImageIntegrationInstalled(m_homePath, m_appImagePath)
            && appImageInInstallFolder(m_homePath, m_appImagePath);
        m_integrationButton->setText(
            installed ? QStringLiteral("Installed")
                      : QStringLiteral("Install Speecher"));
        m_integrationButton->setEnabled(!installed);
    }

    const bool known = m_controller.globalShortcutSupportKnown();
    const bool supported = m_controller.globalShortcutsSupported();
    const bool desktopChooser = m_controller.globalShortcutUsesDesktopChooser();
    // Until the install has moved the image, every shortcut control is
    // premature: the manual command would quote a path the install is about
    // to remove.
    const bool ready = !installRequired();
    const bool portalVisible = ready && (!known || (supported && desktopChooser));
    const bool combinationsAvailable = ready && known && supported && !desktopChooser;
    const bool manualCommand = ready && known && !supported;
    m_portalControls->setVisible(portalVisible);
    m_manualControls->setVisible(manualCommand);
    // The capture handles combinations only where the desktop registers them;
    // a single key is watched by Speecher itself, so it records whenever the
    // step is ready. The lead names what is on offer.
    m_setShortcut->setCombinationsAvailable(combinationsAvailable);
    m_captureLead->setText(captureLead(combinationsAvailable, portalVisible));
    m_captureControls->setVisible(ready && known);
    m_keyHelperControls->setVisible(ready && known && m_waylandSession);
    if (m_waylandSession && ready && known) {
        refreshKeyHelper();
    }
    m_status->setVisible(ready && (!known || supported));
    m_trayNote->setText(linuxTrayShortcutNote(QSystemTrayIcon::isSystemTrayAvailable()));
    m_trayNote->setVisible(ready && known && supported);
    // A manual desktop shortcut can only run the toggle command, and a backend
    // that has already shown it reports no release cannot hold either. Both are
    // states we know; neither is probed for.
    m_holdUnavailableNote->setVisible(manualCommand
                                      || !m_controller.globalShortcutReportsRelease());
    if (!ready) {
        return;
    }

    if (!known) {
        m_chooseShortcut->setEnabled(false);
        m_status->setText(QStringLiteral("Checking your desktop…"));
        return;
    }
    // The portal binder's shortcut() is a placeholder; only its display text
    // carries what the desktop actually assigned.
    m_setShortcut->setShortcutDisplay(desktopChooser
                                          ? m_controller.globalShortcutDisplay()
                                          : m_controller.globalShortcut().displayText());
    if (!supported) {
        return;
    }
    if (desktopChooser) {
        m_chooseShortcut->setEnabled(true);
        const QString display = m_controller.globalShortcutDisplay();
        if (display != m_displayedShortcut) {
            m_displayedShortcut = display;
            m_status->setText(display.isEmpty() ? QString() : shortcutSetStatus(display));
        }
    }
}

void LinuxGlobalShortcutSetupPage::refreshKeyHelper()
{
    const KeywatchSetupStatus status = KeywatchSetup::probe();
    m_keyHelperStatus->setText(status.detail);
    m_keyHelperButton->setEnabled(!status.ready() && !m_keyHelperProgress->isVisible());
    m_keyHelperButton->setText(status.ready() ? QStringLiteral("Key helper ready")
                                              : QStringLiteral("Set up single-key helper"));
}

void LinuxGlobalShortcutSetupPage::showRegistrationResult(bool bound,
                                                           const QString &detail)
{
    m_chooseShortcut->setEnabled(m_controller.globalShortcutsSupported());
    const QString display = m_controller.globalShortcutDisplay();
    if (bound && !display.isEmpty()) {
        m_displayedShortcut = display;
        m_status->setText(shortcutSetStatus(display));
    } else {
        m_status->setText(detail);
    }
    const bool complete = stepComplete();
    if (m_notifiedStepComplete != complete) {
        m_notifiedStepComplete = complete;
        emit stepCompleteChanged();
    }
}

} // namespace speecher
