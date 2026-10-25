#pragma once

#include <QMenu>
#include <QObject>

class QAction;
class QSystemTrayIcon;

namespace speecher {

class ApplicationController;

// The daemon's visible presence on Linux: a status icon that sits in the
// system tray for as long as the process runs. A middle click toggles
// dictation and the context menu offers Start/Stop and Quit. The global
// shortcut only reaches a running process, so the icon doubles as the "the
// shortcut works right now" indicator.
class LinuxTrayIcon final : public QObject {
    Q_OBJECT

public:
    explicit LinuxTrayIcon(ApplicationController *controller, QObject *parent = nullptr);

private:
    void applyState(const QString &stateName);

    ApplicationController *m_controller;
    QMenu m_menu;
    QSystemTrayIcon *m_tray;
    QAction *m_toggleAction;
};

} // namespace speecher
