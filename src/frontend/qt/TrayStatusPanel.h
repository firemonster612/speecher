#pragma once

#include <QFrame>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;

namespace speecher {

class ApplicationController;

// What clicking the tray icon opens: what dictation is doing, Start/Stop, the
// last transcript to copy again, Settings… and Quit Speecher. The Linux
// counterpart of the macOS menu bar popover and the Windows tray flyout.
class TrayStatusPanel final : public QFrame {
    Q_OBJECT

public:
    explicit TrayStatusPanel(ApplicationController *controller, QWidget *parent = nullptr);

    // Shows the panel beside anchor, the tray icon's global geometry (the
    // pointer where the tray does not say). On Wayland, where a client cannot
    // place itself, it goes in the work area's bottom-right corner, beside
    // Plasma's default tray, instead.
    void popUp(const QRect &anchor);

protected:
    bool event(QEvent *event) override;

private:
    void applyState(const QString &stateName);
    void showStatus(const QString &status);
    void showTranscript(const QString &text);
    void fitHeight();

    ApplicationController *m_controller;
    QHBoxLayout *m_headingRow;
    QLabel *m_icon;
    QLabel *m_heading;
    QProgressBar *m_level;
    QPushButton *m_toggle;
    QLabel *m_transcript;
    QPushButton *m_copy;
};

} // namespace speecher
