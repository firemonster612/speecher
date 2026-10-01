#pragma once

#include "app/AppFrontEnd.h"

#include <QObject>
#include <QPointer>

class QWidget;

namespace speecher {

class AppWindow;
class LinuxTrayIcon;
class ApplicationController;
class SetupAssistant;
class TranscribeWindow;
class TranscriberPopup;
class QtFrontEndTestAccess;

// Speecher's user interface on Qt Widgets: the main window, the setup
// assistant, the dictation popup and the Transcribe window opened files use.
class QtFrontEnd final : public QObject, public AppFrontEnd {
    Q_OBJECT

public:
    explicit QtFrontEnd(ApplicationController *controller, QObject *parent = nullptr);
    ~QtFrontEnd() override;

    void showMainWindow() override;
    void hideMainWindow() override;
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    void showTranscribeFiles(const QStringList &paths) override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message, const PopupErrorAction &fix) override;
    void alert() override;
    void notifyIfNoWindowShown(const QString &title, const QString &message, const QString &pageId) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class QtFrontEndTestAccess;

    void wireSessionToPopup();
    void showPopupError(const QString &message, const PopupErrorAction &fix);
    void applyErrorFix();
    void refreshUpdateChip();
    void refreshWhatsNewChip();
    void watchForFirstFrame(QWidget *window);
    TranscribeWindow *transcribeWindow();
    // Screenshot automation; see captureMainWindow.
    static bool clickGrabButtons(QWidget *window);

    ApplicationController *m_controller;
    TranscriberPopup *m_popup;
    AppWindow *m_appWindow = nullptr;
#ifdef Q_OS_LINUX
    LinuxTrayIcon *m_tray = nullptr;
#endif
    QPointer<SetupAssistant> m_setupAssistant;
    TranscribeWindow *m_transcribeWindow = nullptr;
    bool m_reportedReady = false;
    // What the error on the popup offers to fix, for its button.
    PopupErrorAction m_errorFix;
};

} // namespace speecher
