#include "frontend/qt/LinuxTrayIcon.h"

#include <utility>

#include "app/ApplicationController.h"
#include "dictation/DictationTypes.h"

#include <QAction>
#include <QCursor>
#include <QIcon>
#include <QSystemTrayIcon>

namespace speecher {

namespace {

QIcon trayIcon(bool listening)
{
    // The app icon lands in hicolor once Speecher is installed; a themed
    // microphone stands in until then, and says "listening" while the
    // microphone is open (the AppImage bundles breeze as the fallback theme,
    // so both names resolve there too).
    const QIcon microphone = QIcon::fromTheme(QStringLiteral("audio-input-microphone"));
    return listening ? microphone
                     : QIcon::fromTheme(QStringLiteral("io.github.firemonster612.speecher"), microphone);
}

} // namespace

LinuxTrayIcon::LinuxTrayIcon(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_panel(controller)
    , m_tray(new QSystemTrayIcon(this))
{
    m_toggleAction = m_menu.addAction(QString(), controller, &ApplicationController::toggle);
    m_toggleAction->setObjectName(QStringLiteral("trayToggleDictation"));
    QAction *settings = m_menu.addAction(traySettingsCaption(),
                                         controller, &ApplicationController::showSettingsWindow);
    settings->setObjectName(QStringLiteral("traySettings"));
    m_menu.addSeparator();
    QAction *quit = m_menu.addAction(trayQuitCaption(),
                                     controller, &ApplicationController::quitApplication);
    quit->setObjectName(QStringLiteral("trayQuit"));
    m_tray->setContextMenu(&m_menu);

    connect(controller, &ApplicationController::stateChanged,
            this, &LinuxTrayIcon::applyState);
    connect(m_tray, &QSystemTrayIcon::activated,
            this, [this, controller](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) {
                    // A StatusNotifierItem tray reports no geometry, so the
                    // pointer, which just clicked the icon, stands in.
                    const QRect icon = m_tray->geometry();
                    m_panel.popUp(icon.isValid() ? icon : QRect(QCursor::pos(), QSize(1, 1)));
                } else if (reason == QSystemTrayIcon::MiddleClick) {
                    controller->toggle();
                }
            });

    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &LinuxTrayIcon::messageClicked);
    connect(this, &LinuxTrayIcon::messageClicked, this, [this] {
        if (const std::function<void()> clicked = std::exchange(m_messageClicked, {})) {
            clicked();
        }
    });

    applyState(controller->stateName());
    m_tray->show();
}

TrayStatusPanel *LinuxTrayIcon::panel()
{
    return &m_panel;
}

void LinuxTrayIcon::showMessage(const QString &title, const QString &message, std::function<void()> clicked)
{
    m_messageClicked = std::move(clicked);
    m_tray->showMessage(title, message);
    emit messageShown(title, message);
}

void LinuxTrayIcon::applyState(const QString &stateName)
{
    const bool listening = dictationListeningPresentation(stateName);
    const DictationToggleAction toggle = dictationToggleAction(stateName);
    m_toggleAction->setText(toggle.label);
    m_toggleAction->setEnabled(toggle.enabled);
    m_tray->setToolTip(trayToolTip(listening));
    m_tray->setIcon(trayIcon(listening));
}

} // namespace speecher
