#include "ui/setup/LinuxGlobalShortcutSetupPage.h"

#include "app/SetupSteps.h"

#include "app/ApplicationController.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/KeywatchSetup.h"
#include "platform/LinuxDesktopIntegration.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
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

QString checkingDesktopStatus()
{
    return QStringLiteral("Checking your desktop…");
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

// What to press while the capture waits. Where the desktop registers
// combinations the whole range is on offer; elsewhere Speecher can only watch
// a single key, and portal desktops pick combinations through Choose shortcut.
QString captureLead(bool combinationsAvailable)
{
    return combinationsAvailable
        ? globalShortcutPrompt()
        : QStringLiteral("Press a single key, such as Right Alt or F13, to use on its own.");
}

} // namespace

ShortcutCaptureButton::ShortcutCaptureButton(QWidget *parent)
    : QPushButton(globalShortcutSetCaption(), parent)
{
    connect(this, &QPushButton::clicked, this, [this] { setArmed(!m_armed); });
}

// Where the desktop registers no combinations the button can only record a
// single key, and says so beside the desktop's own Choose shortcut.
QString ShortcutCaptureButton::idleText() const
{
    if (!m_combinationsAvailable) {
        return globalShortcutSingleKeyCaption();
    }
    return m_display.isEmpty() ? globalShortcutSetCaption() : globalShortcutChangeCaption();
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
    if (!m_armed) {
        setText(idleText());
    }
}

void ShortcutCaptureButton::setArmed(bool armed)
{
    if (m_armed == armed) {
        return;
    }
    m_armed = armed;
    m_heldModifiers.clear();
    m_pendingModifier = 0;
    setText(armed ? QStringLiteral("Cancel") : idleText());
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
    QWidget *parent,
    Placement placement,
    GlobalShortcutRole role)
    : QWidget(parent)
    , m_controller(controller)
    , m_role(role)
    , m_homePath(QDir::homePath())
    , m_appImagePath(QString::fromLocal8Bit(qgetenv("APPIMAGE")))
    , m_binaryPath(resolvedPath(QCoreApplication::applicationFilePath()))
    , m_waylandSession(isWaylandSession())
    , m_settingsCard(placement == Placement::SettingsCard)
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
    m_integration->setVisible(!m_settingsCard && !m_appImagePath.isEmpty());
    layout->addWidget(m_integration);

    // The shortcut's rows, as card rows: inside a card of their own on the
    // setup step, straight into the Dictation card in settings.
    QFormLayout *rows = nullptr;
    if (m_settingsCard) {
        auto *host = new QWidget(this);
        rows = new QFormLayout(host);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setVerticalSpacing(0);
        settings::configureFormLayout(rows);
        layout->addWidget(host);
    } else {
        QFrame *card = settings::makeSettingsCard(this);
        rows = settings::cardFormLayout(card);
        layout->addWidget(card);
    }
    QWidget *rowHost = rows->parentWidget();

    // Title, current binding, and the buttons that change it. One capture
    // button records the whole range: a key combination or a single key, a
    // bare modifier included, which QKeySequenceEdit cannot report. Portal
    // desktops pick combinations through their own dialog instead.
    auto *trailing = new QWidget(rowHost);
    auto *buttons = new QHBoxLayout(trailing);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(settings::relatedSpacing());
    m_binding = new QLabel(trailing);
    m_binding->setObjectName(QStringLiteral("globalShortcutBinding"));
    buttons->addWidget(m_binding);
    m_chooseShortcut = new QPushButton(globalShortcutChooseCaption(), trailing);
    m_chooseShortcut->setObjectName(QStringLiteral("portalShortcut"));
    buttons->addWidget(m_chooseShortcut);
    m_setShortcut = new ShortcutCaptureButton(trailing);
    m_setShortcut->setObjectName(QStringLiteral("globalShortcutCapture"));
    buttons->addWidget(m_setShortcut);
    m_resetShortcut = new QPushButton(
        globalShortcutResetCaption(ShortcutBinding(GlobalShortcutBinder::defaultShortcut()).displayText()),
        trailing);
    m_resetShortcut->setObjectName(QStringLiteral("resetGlobalShortcut"));
    buttons->addWidget(m_resetShortcut);
    m_clearShortcut = new QPushButton(globalShortcutClearCaption(), trailing);
    m_clearShortcut->setObjectName(QStringLiteral("clearGlobalShortcut"));
    buttons->addWidget(m_clearShortcut);
    m_captureControls = settings::makeRow(shortcutRow().label, shortcutRow().help, trailing, rowHost);
    m_captureControls->setObjectName(QStringLiteral("shortcutCapture"));
    m_description = m_captureControls->findChild<QLabel *>(QStringLiteral("rowDescription"));
    m_description->setObjectName(QStringLiteral("globalShortcutStatus"));
    settings::addCardRow(rows, m_captureControls, rowHost);

    // A refusal or a warning about the key just recorded, under the row.
    m_captureFeedback = guidanceLabel(QString(), rowHost);
    m_captureFeedback->setObjectName(QStringLiteral("shortcutCaptureFeedback"));
    m_captureFeedback->setContentsMargins(settings::rowPadding());
    m_captureFeedback->hide();
    rows->addRow(m_captureFeedback);

    // Wayland's only route to a single key is the privileged key-watch
    // helper. Recording stays enabled without it — combinations need no
    // helper — and a single key recorded too early is refused with the
    // helper's own status, which reads in this row.
    m_keyHelperButton = new QPushButton(QStringLiteral("Set up single-key helper"), rowHost);
    m_keyHelperControls = settings::makeRow(
        QStringLiteral("Single-key helper"),
        QStringLiteral("On Wayland, a single-key shortcut needs a small helper that watches for "
                       "that one key. Setting it up asks for administrator permission once; "
                       "Speecher itself stays unprivileged. The helper only allows keys that "
                       "cannot type text: modifiers, Caps Lock and F13 to F24."),
        m_keyHelperButton, rowHost);
    m_keyHelperControls->setObjectName(QStringLiteral("keyHelperInstall"));
    auto *keyHelperText = m_keyHelperControls->findChild<QWidget *>(QStringLiteral("rowLabelCell"));
    m_keyHelperStatus = new QLabel(keyHelperText);
    m_keyHelperStatus->setWordWrap(true);
    keyHelperText->layout()->addWidget(m_keyHelperStatus);
    m_keyHelperProgress = new QProgressBar(keyHelperText);
    m_keyHelperProgress->setRange(0, 0);
    m_keyHelperProgress->setVisible(false);
    keyHelperText->layout()->addWidget(m_keyHelperProgress);
    settings::addCardRow(rows, m_keyHelperControls, rowHost);

    m_manualControls = new QWidget(rowHost);
    m_manualControls->setObjectName(QStringLiteral("manualShortcut"));
    auto *manualLayout = new QVBoxLayout(m_manualControls);
    manualLayout->setContentsMargins(settings::rowPadding());
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
    settings::addCardRow(rows, m_manualControls, rowHost);

    // The shortcut and its behaviour are set together on the setup step. In
    // settings the General page's own activationMode row edits the setting.
    const SettingsRow &modeRow = setupSchemaRow(QStringLiteral("activationMode"));
    m_activationMode = new QComboBox(rowHost);
    m_activationMode->setObjectName(QStringLiteral("activationMode"));
    for (const RowOption &option : modeRow.options(m_controller.settings()->dictationSnapshot())) {
        m_activationMode->addItem(option.label, option.id);
        m_activationMode->setItemData(m_activationMode->count() - 1, option.help, Qt::ToolTipRole);
    }
    m_activationModeRow = settings::makeRow(modeRow.label, modeRow.help, m_activationMode, rowHost);
    m_activationModeRow->setToolTip(modeRow.tooltip);
    // Reads under the mode's description, where push-to-talk is on offer,
    // because that is the promise it corrects.
    auto *modeText = m_activationModeRow->findChild<QWidget *>(QStringLiteral("rowLabelCell"));
    m_holdUnavailableNote = guidanceLabel(linuxHoldToTalkUnavailableNote(), modeText);
    m_holdUnavailableNote->setObjectName(QStringLiteral("holdToTalkUnavailable"));
    modeText->layout()->addWidget(m_holdUnavailableNote);
    settings::addCardRow(rows, m_activationModeRow, rowHost);
    settings::setCardRowVisible(m_activationModeRow, !m_settingsCard);

    // Not shown on manual-command desktops: their command starts Speecher by
    // itself, so "only while running" would be wrong there.
    m_trayNote = guidanceLabel(QString(), this);
    m_trayNote->setObjectName(QStringLiteral("globalShortcutTrayNote"));
    layout->addWidget(m_trayNote);

    if (!m_settingsCard) {
        layout->addStretch();
    }

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
        refreshDescription();
        if (armed) {
            // A stale refusal from the last attempt would read as a verdict on
            // the capture that is only just starting.
            showCaptureFeedback(QString());
            m_controller.suspendGlobalShortcut();
        } else {
            m_controller.resumeGlobalShortcut();
        }
    });
    connect(m_setShortcut, &ShortcutCaptureButton::unknownKeyPressed, this, [this] {
        showCaptureFeedback(QStringLiteral("That key cannot be a dictation key."));
    });
    connect(m_chooseShortcut, &QPushButton::clicked, this, [this] { chooseShortcut(); });
    connect(m_resetShortcut, &QPushButton::clicked, this, [this] { resetShortcut(); });
    connect(m_clearShortcut, &QPushButton::clicked, this, [this] { clearShortcut(); });
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
            [this](bool bound, const QString &detail, GlobalShortcutRole role) {
                if (role == m_role) {
                    showRegistrationResult(bound, detail);
                }
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

bool LinuxGlobalShortcutSetupPage::installRequired() const
{
    // A command link from an earlier version can point at an image still in
    // Downloads; that is not installed either — the move is part of the deal.
    return !m_settingsCard && !m_appImagePath.isEmpty()
        && (!appImageIntegrationInstalled(m_homePath, m_appImagePath)
            || !appImageInInstallFolder(m_homePath, m_appImagePath));
}

bool LinuxGlobalShortcutSetupPage::stepComplete() const
{
    if (installRequired()) {
        return false;
    }
    if (!m_controller.globalShortcutSupportKnown(m_role)) {
        return false;
    }
    if (!m_controller.globalShortcutsSupported(m_role)) {
        return true;
    }
    return !m_controller.globalShortcutDisplay(m_role).isEmpty();
}

QString LinuxGlobalShortcutSetupPage::blockedReason() const
{
    if (installRequired()) {
        return QStringLiteral(
            "Speecher is not installed yet, so a shortcut would point at the wrong file.");
    }
    if (!m_controller.globalShortcutSupportKnown(m_role)) {
        return QStringLiteral("Speecher is still checking what your desktop supports.");
    }
    return findSetupStep(QStringLiteral("shortcut"))->blocked;
}

void LinuxGlobalShortcutSetupPage::showCaptureFeedback(const QString &text)
{
    m_captureFeedback->setText(text);
    m_captureFeedback->setVisible(!text.isEmpty());
}

void LinuxGlobalShortcutSetupPage::setStatus(const QString &text)
{
    m_statusText = text;
    refreshDescription();
}

void LinuxGlobalShortcutSetupPage::refreshDescription()
{
    m_description->setText(m_setShortcut->armed()      ? captureLead(m_combinationsAvailable)
                           : !m_statusText.isEmpty() ? m_statusText
                                                     : shortcutRow().help);
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
    // told why rather than getting a binding that never fires.
    const QString reason = m_controller.globalShortcutUnsupportedBindingReason(binding, m_role);
    if (!reason.isEmpty()) {
        showCaptureFeedback(reason);
        return;
    }
    QString error;
    if (!m_controller.setGlobalShortcut(binding, &error, m_role)) {
        showCaptureFeedback(
            error.isEmpty() ? QStringLiteral("Couldn't set the shortcut.") : error);
        return;
    }
    showCaptureFeedback(binding.isSingleKey() ? singleKeyTypingWarning(binding) : QString());
    setStatus(shortcutSetStatus(m_controller.globalShortcutDisplay(m_role)));
    refreshControls();
}

void LinuxGlobalShortcutSetupPage::resetShortcut()
{
    applyBinding(ShortcutBinding(GlobalShortcutBinder::defaultShortcut()));
}

// Without combinations to reset to, the single key Speecher watches can only
// be let go; the desktop's own shortcut is the desktop's to change. Removal
// always lets the single key go, and the desktop registration it also drops
// was already given up when the single key took over, so its answer is moot.
void LinuxGlobalShortcutSetupPage::clearShortcut()
{
    if (m_role == GlobalShortcutRole::Cancel) {
        m_controller.setGlobalShortcut({}, nullptr, m_role);
    } else {
        m_controller.removeGlobalShortcutRegistration();
    }
    showCaptureFeedback(QString());
    m_displayedShortcut.clear();
    setStatus(QString());
    refresh();
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
    setStatus(QStringLiteral("Waiting for your desktop…"));
    m_controller.registerGlobalShortcut(m_role);
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

    const bool known = m_controller.globalShortcutSupportKnown(m_role);
    const bool supported = m_controller.globalShortcutsSupported(m_role);
    const bool desktopChooser = m_controller.globalShortcutUsesDesktopChooser(m_role);
    // Until the install has moved the image, every shortcut control is
    // premature: the manual command would quote a path the install is about
    // to remove.
    const bool ready = !installRequired();
    const bool portal = !known || (supported && desktopChooser);
    m_combinationsAvailable = known && supported && !desktopChooser;
    const bool manualCommand = ready && known && !supported;
    settings::setCardRowVisible(m_captureControls, ready);
    settings::setCardRowVisible(m_manualControls, manualCommand && m_role == GlobalShortcutRole::Dictation);
    m_chooseShortcut->setVisible(portal);
    // The capture handles combinations only where the desktop registers them;
    // a single key is watched by Speecher itself, so it records whenever the
    // desktop's answer is in.
    m_setShortcut->setCombinationsAvailable(m_combinationsAvailable);
    m_setShortcut->setVisible(known);
    settings::setCardRowVisible(m_keyHelperControls,
                                ready && known && m_waylandSession && m_role == GlobalShortcutRole::Dictation);
    if (m_waylandSession && ready && known) {
        refreshKeyHelper();
    }
    m_trayNote->setText(linuxTrayShortcutNote(QSystemTrayIcon::isSystemTrayAvailable()));
    m_trayNote->setVisible(!m_settingsCard && ready && known && supported);
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
        m_resetShortcut->hide();
        m_clearShortcut->hide();
        setStatus(checkingDesktopStatus());
        return;
    }
    // The portal binder's shortcut() is a placeholder; only its display text
    // carries what the desktop actually assigned.
    const QString display = desktopChooser ? m_controller.globalShortcutDisplay(m_role)
                                           : m_controller.globalShortcut(m_role).displayText();
    // A manual desktop's own shortcut is out of Speecher's sight, so only
    // where the binding is Speecher's to know does an empty one read as such.
    m_binding->setText(display.isEmpty() ? globalShortcutUnsetText() : display);
    m_binding->setForegroundRole(display.isEmpty() ? QPalette::PlaceholderText : QPalette::WindowText);
    m_binding->setVisible(!display.isEmpty() || supported);
    m_setShortcut->setShortcutDisplay(display);
    const QString defaultDisplay = ShortcutBinding(GlobalShortcutBinder::defaultShortcut()).displayText();
    if (m_role == GlobalShortcutRole::Cancel) {
        m_resetShortcut->hide();
        m_clearShortcut->setVisible(!display.isEmpty());
    } else {
        m_resetShortcut->setVisible(m_combinationsAvailable && display != defaultDisplay);
        m_clearShortcut->setVisible(!m_combinationsAvailable
                                    && m_controller.globalShortcut(m_role).isSingleKey());
    }
    if (m_statusText == checkingDesktopStatus()) {
        setStatus(QString());
    }
    if (desktopChooser && supported) {
        m_chooseShortcut->setEnabled(true);
        if (display != m_displayedShortcut) {
            m_displayedShortcut = display;
            setStatus(display.isEmpty() ? QString() : shortcutSetStatus(display));
        }
    }
}

const SettingsRow &LinuxGlobalShortcutSetupPage::shortcutRow() const
{
    return setupSchemaRow(m_role == GlobalShortcutRole::Cancel ? QStringLiteral("cancelShortcut")
                                                               : QStringLiteral("globalShortcut"));
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
    m_chooseShortcut->setEnabled(m_controller.globalShortcutsSupported(m_role));
    const QString display = m_controller.globalShortcutDisplay(m_role);
    if (bound && !display.isEmpty()) {
        m_displayedShortcut = display;
        setStatus(shortcutSetStatus(display));
    } else {
        setStatus(detail);
    }
    const bool complete = stepComplete();
    if (m_notifiedStepComplete != complete) {
        m_notifiedStepComplete = complete;
        emit stepCompleteChanged();
    }
}

} // namespace speecher
