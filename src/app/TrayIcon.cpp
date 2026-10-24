#include "app/TrayIcon.h"

#include "app/ApplicationController.h"
#include "dictation/DictationTypes.h"

#include <QAction>
#include <QIcon>
#include <QSystemTrayIcon>
#ifdef Q_OS_WIN
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#endif

namespace speecher {

namespace {

// Speecher's own icon, for while nothing is listening or recording.
QIcon idleIcon(const QIcon &microphone)
{
#if defined(Q_OS_MACOS)
    Q_UNUSED(microphone);
    return QIcon(QStringLiteral(":/speecher/tray/speecher-menubar.png"));
#elif defined(Q_OS_WIN)
    Q_UNUSED(microphone);
    // Drawn in the taskbar's text colour. The taskbar has a theme of its own,
    // which can differ from the one apps follow.
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const bool lightTaskbar = personalize.value(QStringLiteral("SystemUsesLightTheme"), 1).toInt() != 0;
    return QIcon(lightTaskbar ? QStringLiteral(":/speecher/tray/tray-dark.ico")
                              : QStringLiteral(":/speecher/tray/tray-light.ico"));
#else
    // The app icon lands in hicolor once Speecher is installed; the
    // microphone stands in until then.
    return QIcon::fromTheme(QStringLiteral("io.github.firemonster612.speecher"), microphone);
#endif
}

QIcon trayIcon(bool listening, bool recording)
{
    // A themed microphone says "listening" while the microphone is open, and
    // the theme's record icon says a recording runs. The AppImage bundles
    // breeze as the fallback theme, so these names resolve there too; on
    // macOS and Windows Qt maps them to the system's own symbols.
    const QIcon microphone = QIcon::fromTheme(QStringLiteral("audio-input-microphone"));
    QIcon icon = listening ? microphone
        : recording        ? QIcon::fromTheme(QStringLiteral("media-record"), microphone)
                           : idleIcon(microphone);
#ifdef Q_OS_MACOS
    // A template image: the menu bar tints it for its appearance.
    icon.setIsMask(true);
#endif
    return icon;
}

} // namespace

TrayIcon::TrayIcon(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_tray(new QSystemTrayIcon(this))
{
#ifndef Q_OS_LINUX
    // A click on the macOS menu bar item opens this menu, not Speecher.
    QAction *open = m_menu.addAction(QStringLiteral("Open Speecher"), controller, &ApplicationController::showMain);
    open->setObjectName(QStringLiteral("trayOpen"));
#endif
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
            this, &TrayIcon::applyState);
    connect(controller, &ApplicationController::recordingChanged, this,
            [this, controller] { applyState(controller->stateName()); });
    connect(m_tray, &QSystemTrayIcon::activated,
            this, [controller](QSystemTrayIcon::ActivationReason reason) {
                // On macOS the click that opens the menu says Trigger too.
#ifndef Q_OS_MACOS
                if (reason == QSystemTrayIcon::Trigger) {
                    controller->showMain();
                }
#endif
                if (reason == QSystemTrayIcon::MiddleClick) {
                    controller->toggle();
                }
            });
#ifdef Q_OS_WIN
    // Picking a theme in Windows' settings changes the taskbar's with the
    // apps', and Qt says when the apps' changes.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this, controller] { applyState(controller->stateName()); });
#endif

    applyState(controller->stateName());
    m_tray->show();
}

void TrayIcon::applyState(const QString &stateName)
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
