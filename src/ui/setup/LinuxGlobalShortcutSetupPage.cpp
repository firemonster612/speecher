#include "ui/setup/LinuxGlobalShortcutSetupPage.h"

#include "app/ApplicationController.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace speecher {
namespace {

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

    m_confirmation = wrappedLabel(QString(), content);
    layout->addWidget(m_confirmation);
    m_moreOptions = new QToolButton(content);
    m_moreOptions->setText(QStringLiteral("More options"));
    m_moreOptions->setCheckable(true);
    m_moreOptions->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_moreOptions->setArrowType(Qt::RightArrow);
    layout->addWidget(m_moreOptions, 0, Qt::AlignLeft);

    m_options = new QWidget(content);
    auto *optionsLayout = new QVBoxLayout(m_options);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(10);
    layout->addWidget(m_options);
    connect(m_moreOptions, &QToolButton::toggled, this, [this](bool expanded) {
        m_moreOptions->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_options->setVisible(expanded);
    });

    m_integrationGroup = new QGroupBox(QStringLiteral("Add to your system"), m_options);
    auto *integrationLayout = new QVBoxLayout(m_integrationGroup);
    integrationLayout->addWidget(wrappedLabel(
        QStringLiteral("Puts Speecher in your app menu and lets you run `speecher` in a terminal. "
                       "Updates keep working. Moving the AppImage file elsewhere breaks it — "
                       "run this again if you do."),
        m_integrationGroup));
    auto *integrationRow = new QHBoxLayout;
    m_integrationButton = new QPushButton(
        QStringLiteral("Add Speecher to your app menu"), m_integrationGroup);
    m_integrationStatus = new QLabel(m_integrationGroup);
    integrationRow->addWidget(m_integrationButton);
    integrationRow->addWidget(m_integrationStatus, 1);
    integrationLayout->addLayout(integrationRow);
    m_integrationGroup->setVisible(!m_appImagePath.isEmpty());
    optionsLayout->addWidget(m_integrationGroup);
    connect(m_integrationButton, &QPushButton::clicked,
            this, [this] { installIntegration(); });

    auto *registrationGroup = new QGroupBox(QStringLiteral("Global Shortcut"), m_options);
    auto *registrationLayout = new QVBoxLayout(registrationGroup);
    registrationLayout->addWidget(wrappedLabel(
        QStringLiteral("Register a Global Shortcut so you can start dictation from anywhere."),
        registrationGroup));
    auto *registrationRow = new QHBoxLayout;
    m_registerButton = new QPushButton(QStringLiteral("Register shortcut"), registrationGroup);
    m_registrationStatus = wrappedLabel(QString(), registrationGroup);
    registrationRow->addWidget(m_registerButton);
    registrationRow->addWidget(m_registrationStatus, 1);
    registrationLayout->addLayout(registrationRow);
    optionsLayout->addWidget(registrationGroup);
    connect(m_registerButton, &QPushButton::clicked,
            this, [this] { registerShortcut(); });
    connect(&m_controller,
            &ApplicationController::globalShortcutRegistrationFinished,
            this,
            [this](bool bound, const QString &detail) {
                showRegistrationResult(bound, detail);
            });

    m_manualGroup = new QGroupBox(QStringLiteral("Manual setup"), m_options);
    auto *manualLayout = new QVBoxLayout(m_manualGroup);
    const bool gnome = qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
        QStringLiteral("GNOME"), Qt::CaseInsensitive);
    manualLayout->addWidget(wrappedLabel(
        gnome
            ? QStringLiteral("Automatic setup doesn't work on GNOME yet, so set the shortcut "
                             "up by hand:\n\n"
                             "Open Settings > Keyboard > View and Customize Shortcuts > "
                             "Custom Shortcuts > +. Paste the command, then pick a key combo.")
            : QStringLiteral("In your desktop's keyboard settings, bind a key combo to run:"),
        m_manualGroup));
    auto *commandRow = new QHBoxLayout;
    m_command = new QLabel(m_manualGroup);
    m_command->setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::TextSelectableByKeyboard);
    auto *copy = new QToolButton(m_manualGroup);
    copy->setText(QStringLiteral("Copy"));
    copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    commandRow->addWidget(m_command, 1);
    commandRow->addWidget(copy);
    manualLayout->addLayout(commandRow);
    optionsLayout->addWidget(m_manualGroup);
    connect(copy, &QToolButton::clicked, this, [this, copy] {
        QGuiApplication::clipboard()->setText(m_command->text());
        copy->setIcon(QIcon::fromTheme(
            QStringLiteral("checkmark"),
            QIcon::fromTheme(QStringLiteral("dialog-ok-apply"))));
        QTimer::singleShot(1500, copy, [copy] {
            copy->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
        });
    });

    auto *removalGroup = new QGroupBox(QStringLiteral("Removal"), m_options);
    auto *removalLayout = new QVBoxLayout(removalGroup);
    m_removal = wrappedLabel(QString(), removalGroup);
    m_removal->setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::TextSelectableByKeyboard);
    removalLayout->addWidget(m_removal);
    optionsLayout->addWidget(removalGroup);
    optionsLayout->addStretch();
    layout->addStretch();

    updateIntegrationState();
    updateInstructionCommand();
    refreshRegistrationState();
    connect(&m_controller,
            &ApplicationController::globalShortcutChanged,
            this,
            [this] { refreshRegistrationState(); });
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
        updateRemovalGuide();
        return;
    }
    const bool installed = appImageIntegrationInstalled(m_homePath, m_appImagePath);
    m_integrationButton->setText(
        installed ? QStringLiteral("Installed")
                  : QStringLiteral("Add Speecher to your app menu"));
    m_integrationButton->setEnabled(!installed);
    if (installed) {
        m_integrationStatus->clear();
    }
    updateRemovalGuide();
}

void LinuxGlobalShortcutSetupPage::updateInstructionCommand()
{
    m_command->setText(globalShortcutInstructionCommand(
        m_homePath, m_appImagePath, m_binaryPath));
}

void LinuxGlobalShortcutSetupPage::updateRemovalGuide()
{
    QStringList lines;
    if (!m_appImagePath.isEmpty()
        && appImageIntegrationInstalled(m_homePath, m_appImagePath)) {
        lines << QStringLiteral("To remove Speecher's shortcut and menu entry:")
              << QStringLiteral(
                     "Delete `~/.local/bin/speecher` and "
                     "`~/.local/share/applications/io.github.firemonster612.speecher.desktop`, "
                     "then `~/.local/share/icons/hicolor/scalable/apps/"
                     "io.github.firemonster612.speecher.svg`.");
    }
    lines << QStringLiteral("Remove the Global Shortcut in your desktop's keyboard settings.")
          << QStringLiteral(
                 "If your desktop remembers that you declined the shortcut, reset it with "
                 "`flatpak permission-reset io.github.firemonster612.speecher`.");
    m_removal->setText(lines.join(QLatin1Char('\n')));
}

void LinuxGlobalShortcutSetupPage::refreshRegistrationState()
{
    const QString display = m_controller.globalShortcutDisplay();
    const bool supported = m_controller.globalShortcutsSupported();
    m_registerButton->setEnabled(supported);
    showWorkingState(display);
    if (!display.isEmpty()) {
        m_registrationStatus->setText(QStringLiteral("Try it now: %1").arg(display));
        m_manualGroup->hide();
        return;
    }
    if (!supported) {
        m_registrationStatus->setText(m_controller.globalShortcutUnsupportedReason());
        m_manualGroup->show();
    } else if (m_plasma) {
        m_registrationStatus->setText(
            QStringLiteral("No Global Shortcut is set yet. Change it anytime in System Settings > "
                           "Shortcuts."));
        m_manualGroup->hide();
    } else {
        m_registrationStatus->setText(QStringLiteral(
            "Click Register and your desktop will ask you to pick a key combination."));
        m_manualGroup->hide();
    }
}

void LinuxGlobalShortcutSetupPage::showWorkingState(const QString &display)
{
    const bool working = !display.isEmpty();
    if (working) {
        m_moreOptions->setChecked(false);
    }
    m_confirmation->setVisible(working);
    m_confirmation->setText(
        working
            ? QStringLiteral("Global Shortcut is set to %1. Try it now.").arg(display)
            : QString());
    m_moreOptions->setVisible(working);
    m_options->setVisible(!working || m_moreOptions->isChecked());
}

void LinuxGlobalShortcutSetupPage::showRegistrationResult(bool bound,
                                                          const QString &detail)
{
    if (bound) {
        refreshRegistrationState();
        return;
    }
    refreshRegistrationState();
    m_registrationStatus->setText(
        detail.isEmpty()
            ? QStringLiteral("Couldn't set the shortcut. Set one up by hand below.")
            : detail);
    m_manualGroup->show();
    const QString existing = m_controller.globalShortcutDisplay();
    if (!existing.isEmpty()) {
        showWorkingState(existing);
        m_moreOptions->setChecked(true);
    }
}

} // namespace speecher
