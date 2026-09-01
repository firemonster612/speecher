#include "ui/setup/LinuxGlobalShortcutSetupPage.h"

#include "app/ApplicationController.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace speecher {
namespace {

QString resolvedPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

} // namespace

LinuxGlobalShortcutSetupPage::LinuxGlobalShortcutSetupPage(
    ApplicationController &controller,
    QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_homePath(QDir::homePath())
    , m_appImagePath(QString::fromLocal8Bit(qgetenv("APPIMAGE")))
    , m_binaryPath(resolvedPath(QCoreApplication::applicationFilePath()))
    , m_plasma(qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
          QStringLiteral("KDE"), Qt::CaseInsensitive))
{
    if (!m_appImagePath.isEmpty()) {
        m_appImagePath = resolvedPath(m_appImagePath);
    }

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    pageLayout->addWidget(scroll);

    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);
    scroll->setWidget(content);

    m_integrationGroup = new QGroupBox(QStringLiteral("System integration"), content);
    auto *integrationLayout = new QVBoxLayout(m_integrationGroup);
    integrationLayout->addWidget(wrappedLabel(
        QStringLiteral("Creates ~/.local/bin/speecher and adds Speecher to your app menu. "
                       "The link survives updates that replace this AppImage in place. "
                       "Moving the AppImage breaks the link."),
        m_integrationGroup));
    auto *integrationRow = new QHBoxLayout;
    m_integrationButton = new QPushButton(
        QStringLiteral("Add Speecher to PATH and app menu"), m_integrationGroup);
    m_integrationStatus = new QLabel(m_integrationGroup);
    integrationRow->addWidget(m_integrationButton);
    integrationRow->addWidget(m_integrationStatus, 1);
    integrationLayout->addLayout(integrationRow);
    m_integrationGroup->setVisible(!m_appImagePath.isEmpty());
    layout->addWidget(m_integrationGroup);
    connect(m_integrationButton, &QPushButton::clicked,
            this, [this] { installIntegration(); });

    auto *registrationGroup = new QGroupBox(QStringLiteral("Global Shortcut"), content);
    auto *registrationLayout = new QVBoxLayout(registrationGroup);
    registrationLayout->addWidget(wrappedLabel(
        QStringLiteral("Register a Global Shortcut so you can start dictation from anywhere."),
        registrationGroup));
    auto *registrationRow = new QHBoxLayout;
    m_registerButton = new QPushButton(QStringLiteral("Register"), registrationGroup);
    m_registrationStatus = wrappedLabel(QString(), registrationGroup);
    registrationRow->addWidget(m_registerButton);
    registrationRow->addWidget(m_registrationStatus, 1);
    registrationLayout->addLayout(registrationRow);
    layout->addWidget(registrationGroup);
    connect(m_registerButton, &QPushButton::clicked,
            this, [this] { registerShortcut(); });
    connect(&m_controller,
            &ApplicationController::globalShortcutRegistrationFinished,
            this,
            [this](bool bound, const QString &detail) {
                showRegistrationResult(bound, detail);
            });

    m_manualGroup = new QGroupBox(QStringLiteral("Manual setup"), content);
    auto *manualLayout = new QVBoxLayout(m_manualGroup);
    const bool gnome = qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
        QStringLiteral("GNOME"), Qt::CaseInsensitive);
    manualLayout->addWidget(wrappedLabel(
        gnome
            ? QStringLiteral("Open Settings > Keyboard > View and Customize Shortcuts > "
                             "Custom Shortcuts > +. Paste the command, then pick a key combo.\n\n"
                             "On GNOME 50, the portal route is currently broken upstream, "
                             "so these manual steps are the reliable path there.")
            : QStringLiteral("In your desktop's keyboard settings, bind a key combo to run:"),
        m_manualGroup));
    auto *commandRow = new QHBoxLayout;
    m_command = new QLabel(m_manualGroup);
    m_command->setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::TextSelectableByKeyboard);
    auto *copy = new QPushButton(QStringLiteral("Copy"), m_manualGroup);
    commandRow->addWidget(m_command, 1);
    commandRow->addWidget(copy);
    manualLayout->addLayout(commandRow);
    layout->addWidget(m_manualGroup);
    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_command->text());
    });

    auto *removalGroup = new QGroupBox(QStringLiteral("Removal"), content);
    auto *removalLayout = new QVBoxLayout(removalGroup);
    auto *removal = wrappedLabel(
        QStringLiteral("Delete ~/.local/bin/speecher, "
                       "~/.local/share/applications/io.github.firemonster612.speecher.desktop, "
                       "and ~/.local/share/icons/hicolor/scalable/apps/"
                       "io.github.firemonster612.speecher.svg.\n"
                       "Remove the Global Shortcut in your desktop's keyboard settings.\n"
                       "Portal users can reset a stored denial with:\n"
                       "flatpak permission-reset io.github.firemonster612.speecher\n"
                       "This also works for non-Flatpak apps because they share the permission store."),
        removalGroup);
    removal->setTextInteractionFlags(Qt::TextSelectableByMouse
                                     | Qt::TextSelectableByKeyboard);
    removalLayout->addWidget(removal);
    layout->addWidget(removalGroup);
    layout->addStretch();

    updateIntegrationState();
    updateInstructionCommand();
    const bool supported = m_controller.globalShortcutsSupported();
    m_registerButton->setEnabled(supported);
    if (!supported) {
        m_registrationStatus->setText(
            m_controller.globalShortcutUnsupportedReason());
        m_manualGroup->show();
    } else if (m_plasma) {
        const QString sequence = m_controller.globalShortcutDisplay();
        m_registrationStatus->setText(
            sequence.isEmpty()
                ? QStringLiteral("No Global Shortcut is currently bound. Change it anytime in "
                                 "System Settings > Shortcuts.")
                : QStringLiteral("Currently bound: %1. Change it anytime in System Settings > "
                                 "Shortcuts.").arg(sequence));
        m_manualGroup->hide();
    } else {
        m_registrationStatus->setText(
            QStringLiteral("Your desktop will show its own shortcut dialog."));
        m_manualGroup->hide();
    }
}

void LinuxGlobalShortcutSetupPage::installIntegration()
{
    QString error;
    const bool installed = installAppImageIntegration(
        m_homePath,
        m_appImagePath,
        QCoreApplication::applicationDirPath(),
        &error);
    if (installed) {
        updateIntegrationState();
    } else {
        m_integrationStatus->setText(error);
    }
    updateInstructionCommand();
}

void LinuxGlobalShortcutSetupPage::registerShortcut()
{
    m_registerButton->setEnabled(false);
    m_registrationStatus->setText(
        m_plasma ? QStringLiteral("Registering…")
                 : QStringLiteral("Waiting for your desktop…"));
    m_controller.registerGlobalShortcut();
}

void LinuxGlobalShortcutSetupPage::updateIntegrationState()
{
    if (m_appImagePath.isEmpty()) {
        return;
    }
    const bool installed = appImageIntegrationInstalled(m_homePath, m_appImagePath);
    m_integrationButton->setText(
        installed ? QStringLiteral("Installed")
                  : QStringLiteral("Add Speecher to PATH and app menu"));
    m_integrationButton->setEnabled(!installed);
    if (installed) {
        m_integrationStatus->setText(QStringLiteral("Installed"));
    }
}

void LinuxGlobalShortcutSetupPage::updateInstructionCommand()
{
    m_command->setText(globalShortcutInstructionCommand(
        m_homePath, m_appImagePath, m_binaryPath));
}

void LinuxGlobalShortcutSetupPage::showRegistrationResult(bool bound,
                                                          const QString &detail)
{
    m_registerButton->setEnabled(m_controller.globalShortcutsSupported());
    if (bound) {
        m_registrationStatus->setText(
            QStringLiteral("Try it now: %1").arg(detail));
        m_manualGroup->hide();
        return;
    }
    m_registrationStatus->setText(
        detail.isEmpty() ? QStringLiteral("Global Shortcut registration failed") : detail);
    m_manualGroup->show();
}

} // namespace speecher
