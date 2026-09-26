#pragma once

#include "app/AppFrontEnd.h"

#include <QObject>
#include <QPointer>

class QWidget;

namespace speecher {

class AppWindow;
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
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    void showTranscribeFiles(const QStringList &paths) override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message) override;
    void alert() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class QtFrontEndTestAccess;

    void wireSessionToPopup();
    void refreshUpdateChip();
    void refreshWhatsNewChip();
    void watchForFirstFrame(QWidget *window);
    TranscribeWindow *transcribeWindow();

    ApplicationController *m_controller;
    TranscriberPopup *m_popup;
    AppWindow *m_appWindow = nullptr;
    QPointer<SetupAssistant> m_setupAssistant;
    TranscribeWindow *m_transcribeWindow = nullptr;
    bool m_reportedReady = false;
};

} // namespace speecher
