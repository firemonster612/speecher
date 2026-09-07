#include "ui/setup/LinuxGlobalShortcutSetupPage.h"

#include "app/ApplicationController.h"
#include "platform/LinuxDesktopIntegration.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace speecher {
namespace {

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

} // namespace

QString linuxGlobalShortcutManualInstruction()
{
    return QStringLiteral(
        "Speecher can't register a shortcut on this desktop. In your desktop's keyboard "
        "settings, add a shortcut that runs this command:");
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

    m_keySequenceControls = new QWidget(this);
    m_keySequenceControls->setObjectName(QStringLiteral("keySequenceShortcut"));
    auto *keyLayout = new QVBoxLayout(m_keySequenceControls);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->addWidget(guidanceLabel(
        QStringLiteral("Press the keys you want to use for dictation."),
        m_keySequenceControls));
    auto *keyRow = new QHBoxLayout;
    m_sequence = new QKeySequenceEdit(m_keySequenceControls);
    m_sequence->setObjectName(QStringLiteral("globalShortcutSequence"));
    m_setShortcut = new QPushButton(QStringLiteral("Set shortcut"), m_keySequenceControls);
    keyRow->addWidget(m_sequence, 1);
    keyRow->addWidget(m_setShortcut);
    keyLayout->addLayout(keyRow);
    layout->addWidget(m_keySequenceControls);

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

    layout->addStretch();

    connect(m_sequence,
            &QKeySequenceEdit::keySequenceChanged,
            m_setShortcut,
            [this](const QKeySequence &sequence) {
                m_setShortcut->setEnabled(
                    !sequence.isEmpty() && sequence != m_controller.globalShortcut());
            });
    connect(m_setShortcut, &QPushButton::clicked, this, [this] { setShortcut(); });
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

void LinuxGlobalShortcutSetupPage::setShortcut()
{
    QString error;
    if (!m_controller.setGlobalShortcut(m_sequence->keySequence(), &error)) {
        m_status->setText(error.isEmpty() ? QStringLiteral("Couldn't set the shortcut.")
                                          : error);
        return;
    }
    m_setShortcut->setEnabled(false);
    m_status->setText(shortcutSetStatus(m_controller.globalShortcutDisplay()));
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
    m_keySequenceControls->setVisible(ready && known && supported && !desktopChooser);
    m_portalControls->setVisible(ready && (!known || (supported && desktopChooser)));
    m_manualControls->setVisible(ready && known && !supported);
    m_status->setVisible(ready && (!known || supported));
    if (!ready) {
        return;
    }

    if (!known) {
        m_chooseShortcut->setEnabled(false);
        m_status->setText(QStringLiteral("Checking your desktop…"));
        return;
    }
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
        return;
    }

    if (!m_sequence->hasFocus()) {
        const QSignalBlocker blocker(m_sequence);
        m_sequence->setKeySequence(m_controller.globalShortcut());
    }
    m_setShortcut->setEnabled(
        !m_sequence->keySequence().isEmpty()
        && m_sequence->keySequence() != m_controller.globalShortcut());
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
