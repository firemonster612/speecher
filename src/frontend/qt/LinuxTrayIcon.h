#pragma once

#include <QMenu>
#include <QObject>

#include <functional>

class QAction;
class QSystemTrayIcon;

namespace speecher {

class ApplicationController;

// The daemon's visible presence: a status icon that sits in the system tray
// for as long as the process runs, mirroring the Windows tray icon and the
// macOS menu bar extra. The global shortcut only reaches a running process,
// so the icon doubles as the "the shortcut works right now" indicator the
// setup assistant points at.
class LinuxTrayIcon final : public QObject {
    Q_OBJECT

public:
    explicit LinuxTrayIcon(ApplicationController *controller, QObject *parent = nullptr);

    // Clicking the balloon runs clicked.
    void showMessage(const QString &title, const QString &message, std::function<void()> clicked);

signals:
    // A balloon went up; tests observe it, since the tray itself cannot be.
    void messageShown(const QString &title, const QString &message);
    // The last balloon was clicked.
    void messageClicked();

private:
    void applyState(const QString &stateName);

    QMenu m_menu;
    QSystemTrayIcon *m_tray;
    QAction *m_toggleAction;
    std::function<void()> m_messageClicked;
};

} // namespace speecher
