#pragma once

#include <QMenu>
#include <QObject>

class QAction;
class QSystemTrayIcon;

namespace speecher {

class ApplicationController;

// The daemon's visible presence: a status icon in the system tray on Linux
// and Windows, and in the menu bar on macOS, for as long as the process runs.
// A click opens Speecher (on macOS a click opens the menu, which offers Open
// Speecher; Windows offers it too), a middle click toggles dictation and the
// menu offers Start/Stop, Settings and Quit. The global shortcut only reaches
// a running process, so the icon doubles as the "the shortcut works right
// now" indicator.
class TrayIcon final : public QObject {
    Q_OBJECT

public:
    explicit TrayIcon(ApplicationController *controller, QObject *parent = nullptr);

private:
    void applyState(const QString &stateName);

    ApplicationController *m_controller;
    QMenu m_menu;
    QSystemTrayIcon *m_tray;
    QAction *m_toggleAction;
};

} // namespace speecher
