#pragma once

#include "frontend/qt/TrayStatusPanel.h"

#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

#include <functional>

class QAction;

namespace speecher {

class ApplicationController;

// The daemon's visible presence: a status icon that sits in the system tray
// for as long as the process runs, mirroring the Windows tray icon and the
// macOS menu bar extra. A click opens the status panel, a middle click
// toggles dictation, and the context menu offers the panel's actions. The
// global shortcut only reaches a running process, so the icon doubles as the
// "the shortcut works right now" indicator the setup assistant points at.
class LinuxTrayIcon final : public QObject {
    Q_OBJECT

public:
    explicit LinuxTrayIcon(ApplicationController *controller, QObject *parent = nullptr);

    // Clicking the balloon runs clicked, once; a later balloon replaces it.
    void showMessage(const QString &title, const QString &message, std::function<void()> clicked);

    TrayStatusPanel *panel();

signals:
    // A balloon went up; tests observe it, since the tray itself cannot be.
    void messageShown(const QString &title, const QString &message);
    // The last balloon was clicked.
    void messageClicked();

private:
    void applyState(const QString &stateName);

    ApplicationController *m_controller;
    QMenu m_menu;
    TrayStatusPanel m_panel;
    QSystemTrayIcon *m_tray;
    QAction *m_toggleAction;
    std::function<void()> m_messageClicked;
};

} // namespace speecher
