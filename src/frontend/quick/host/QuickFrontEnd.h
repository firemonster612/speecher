#pragma once

#include "app/AppFrontEnd.h"
#include "frontend/quick/host/QuickAccounts.h"
#include "frontend/quick/host/QuickAppearance.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickDictation.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickInsights.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickStatus.h"
#include "frontend/quick/host/QuickUpdates.h"
#include "frontend/quick/host/QuickTranscription.h"
#include "frontend/quick/host/QuickSystem.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>

class QQuickWindow;

namespace speecher {

class ApplicationController;
class QuickPopupWindow;
class TrayIcon;

// The Qt Quick UI, shown by the daemon in its own process
// (docs/adr/0009-one-quick-ui-inside-the-daemon.md). It owns the one QML
// engine and the Speecher.Backend objects the pages read, and opens the main
// window and the setup assistant when the controller asks: at most one of
// each. Closing the main window hides it, and the daemon runs on; closing
// the setup assistant ends it, finished or not. The dictation popup has its
// own window, shown whenever the session asks, Main open or not.
// Notifications come from the tray icon. It attaches itself to the controller
// and detaches before it goes.
class QuickFrontEnd final : public QObject, public AppFrontEnd {
    Q_OBJECT

public:
    // tray must outlive the front end.
    QuickFrontEnd(ApplicationController *controller, TrayIcon *tray, QObject *parent = nullptr);
    ~QuickFrontEnd() override;

    void showMainWindow() override;
    void hideMainWindow() override;
    // Settings are pages of the main window.
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    // Opens the Transcribe page with these files added to its list.
    void showTranscribeFiles(const QStringList &paths) override;
    void showDictationError(const QString &message, const PopupErrorAction &fix = {}) override;
    void alert() override;
    void notifyIfNoWindowShown(const QString &title, const QString &message, const QString &pageId) override;

protected:
    // Turns closing either window into hiding it.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void finishSetup();

private:
    // Makes a window from Speecher.App, or logs why it could not.
    QQuickWindow *createWindow(const QString &typeName);
    void setupClosed();
    // Main, on pageId, or on the page it is on when pageId is empty.
    void showPage(const QString &pageId);
    // The page Main shows, or opens on.
    void setPage(const QString &pageId);

    ApplicationController *m_controller;
    TrayIcon *m_tray;
    // Before the engine, which reads them until it goes.
    QuickSettings m_settings;
    QuickAppearance m_appearance;
    QuickAudio m_audio;
    QuickStatus m_status;
    QuickDictation m_dictation;
    QuickUpdates m_updates;
    QuickTranscription m_transcription;
    QuickSystem m_system;
    QuickModels m_models;
    QuickInsights m_insights;
    QuickAccounts m_accounts;
    QuickLibrary::ControllerScope m_library;
    QQmlEngine m_engine;
    QObject *m_themeSetting = nullptr;
    QuickPopupWindow *m_popup = nullptr;
    QPointer<QQuickWindow> m_main;
    QPointer<QQuickWindow> m_setup;
};

} // namespace speecher
