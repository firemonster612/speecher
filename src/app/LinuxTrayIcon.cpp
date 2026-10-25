#include "app/LinuxTrayIcon.h"

#include "app/ApplicationController.h"
#include "dictation/DictationTypes.h"

#include <QAction>
#include <QIcon>
#include <QSystemTrayIcon>

namespace speecher {

namespace {

QIcon trayIcon(bool listening, bool recording)
{
    // The app icon lands in hicolor once Speecher is installed; a themed
    // microphone stands in until then, and says "listening" while the
    // microphone is open, and the theme's record icon says a recording runs
    // (the AppImage bundles breeze as the fallback theme, so these names
    // resolve there too).
    const QIcon microphone = QIcon::fromTheme(QStringLiteral("audio-input-microphone"));
    if (listening) {
        return microphone;
    }
    if (recording) {
        return QIcon::fromTheme(QStringLiteral("media-record"), microphone);
    }
    return QIcon::fromTheme(QStringLiteral("io.github.firemonster612.speecher"), microphone);
}

} // namespace

LinuxTrayIcon::LinuxTrayIcon(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_tray(new QSystemTrayIcon(this))
{
    m_toggleAction = m_menu.addAction(QString(), controller, &ApplicationController::toggle);
    m_toggleAction->setObjectName(QStringLiteral("trayToggleDictation"));
    QAction *settings = m_menu.addAction(QStringLiteral("Settings"), controller, &ApplicationController::showSettings);
    settings->setObjectName(QStringLiteral("traySettings"));
    m_menu.addSeparator();
    QAction *quit = m_menu.addAction(trayQuitCaption(),
                                     controller, &ApplicationController::quitApplication);
    quit->setObjectName(QStringLiteral("trayQuit"));
    m_tray->setContextMenu(&m_menu);

    connect(controller, &ApplicationController::stateChanged,
            this, &LinuxTrayIcon::applyState);
    connect(controller, &ApplicationController::recordingChanged, this,
            [this, controller] { applyState(controller->stateName()); });
    connect(m_tray, &QSystemTrayIcon::activated,
            this, [controller](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) {
                    controller->showMain();
                } else if (reason == QSystemTrayIcon::MiddleClick) {
                    controller->toggle();
                }
            });

    applyState(controller->stateName());
    m_tray->show();
}

void LinuxTrayIcon::applyState(const QString &stateName)
{
    const bool listening = dictationListeningPresentation(stateName);
    const bool recording = m_controller->isRecording();
    const DictationToggleAction toggle = dictationToggleAction(stateName);
    m_toggleAction->setText(toggle.label);
    m_toggleAction->setEnabled(toggle.enabled);
    m_tray->setToolTip(trayToolTip(listening, recording));
    m_tray->setIcon(trayIcon(listening, recording));
}

} // namespace speecher
