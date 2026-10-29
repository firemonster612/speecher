#include "app/TrayIcon.h"

#include "app/ApplicationController.h"
#include "dictation/DictationTypes.h"

#include <QAction>
#include <QIcon>
#include <QSystemTrayIcon>
#ifdef Q_OS_WIN
#include "platform/win/WinBroadcasts.h"

#include <QPainter>
#include <QPixmap>
#include <QSettings>
#endif

namespace speecher {

namespace {

#if defined(Q_OS_MACOS)
QIcon trayIcon(bool listening, bool recording)
{
    // On macOS Qt maps the theme's names to the system's own symbols.
    const QIcon microphone = QIcon::fromTheme(QStringLiteral("audio-input-microphone"));
    QIcon icon = listening ? microphone
        : recording        ? QIcon::fromTheme(QStringLiteral("media-record"), microphone)
                           : QIcon(QStringLiteral(":/speecher/tray/speecher-menubar.png"));
    // A template image: the menu bar tints it for its appearance.
    icon.setIsMask(true);
    return icon;
}
#elif defined(Q_OS_WIN)
// A Segoe Fluent Icons glyph, as Windows' own tray icons draw them.
QIcon glyphIcon(char16_t glyph, const QColor &color)
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QFont font(QStringLiteral("Segoe Fluent Icons"));
    font.setPixelSize(24);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(glyph)));
    return QIcon(pixmap);
}

QIcon trayIcon(bool listening, bool recording)
{
    // Drawn in the taskbar's text colour. The taskbar has a theme of its own,
    // which can differ from the one apps follow.
    const QSettings personalize(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const bool lightTaskbar = personalize.value(QStringLiteral("SystemUsesLightTheme"), 1).toInt() != 0;
    if (listening || recording) {
        // Microphone while listening, Record while only a recording runs.
        return glyphIcon(listening ? u'\uE720' : u'\uE7C8', lightTaskbar ? Qt::black : Qt::white);
    }
    return QIcon(lightTaskbar ? QStringLiteral(":/speecher/tray/tray-dark.ico")
                              : QStringLiteral(":/speecher/tray/tray-light.ico"));
}
#else
QIcon trayIcon(bool listening, bool recording)
{
    // A themed microphone says "listening" while the microphone is open, and
    // the theme's record icon says a recording runs. The AppImage bundles
    // breeze as the fallback theme, so these names resolve there too. The app
    // icon lands in hicolor once Speecher is installed; the microphone stands
    // in until then.
    const QIcon microphone = QIcon::fromTheme(QStringLiteral("audio-input-microphone"));
    return listening ? microphone
        : recording  ? QIcon::fromTheme(QStringLiteral("media-record"), microphone)
                     : QIcon::fromTheme(QStringLiteral("io.github.firemonster612.speecher"), microphone);
}
#endif

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
            [this, controller] { applyState(controller->dictationState()); });
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
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (m_messageClicked) {
            m_messageClicked();
        }
    });
#ifdef Q_OS_WIN
    win::onThemeChanged(this, [this, controller] { applyState(controller->dictationState()); });
#endif

    applyState(controller->dictationState());
    m_tray->show();
}

void TrayIcon::applyState(DictationState state)
{
    const bool listening = dictationListeningPresentation(state);
    const bool recording = m_controller->isRecording();
    const DictationToggleAction toggle = dictationToggleAction(state);
    m_toggleAction->setText(toggle.label);
    m_toggleAction->setEnabled(toggle.enabled);
    m_tray->setToolTip(trayToolTip(listening, recording));
    m_tray->setIcon(trayIcon(listening, recording));
}

void TrayIcon::showMessage(const QString &title, const QString &message, std::function<void()> clicked)
{
    m_messageClicked = std::move(clicked);
    m_tray->showMessage(title, message);
}

} // namespace speecher
