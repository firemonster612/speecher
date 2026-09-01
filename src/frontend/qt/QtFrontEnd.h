#pragma once

#include "app/AppFrontEnd.h"

#include <QObject>
#include <QPointer>

class QWidget;

namespace speecher {

class AppWindow;
class ApplicationController;
class SetupAssistant;
class TranscriberPopup;

// Speecher's user interface on Qt Widgets: the main window, the setup
// assistant and the dictation popup.
class QtFrontEnd final : public QObject, public AppFrontEnd {
    Q_OBJECT

public:
    explicit QtFrontEnd(ApplicationController *controller, QObject *parent = nullptr);
    ~QtFrontEnd() override;

    void showMainWindow() override;
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message) override;
    void alert() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void wireSessionToPopup();
    void watchForFirstFrame(QWidget *window);

    ApplicationController *m_controller;
    TranscriberPopup *m_popup;
    AppWindow *m_appWindow = nullptr;
    QPointer<SetupAssistant> m_setupAssistant;
    bool m_reportedReady = false;
};

} // namespace speecher
