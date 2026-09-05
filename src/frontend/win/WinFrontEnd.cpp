#include "frontend/win/WinFrontEnd.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/SettingsStore.h"
#include "frontend/win/DictationPanel.h"
#include "frontend/win/SettingsWindow.h"
#include "frontend/win/SetupWindow.h"
#include "frontend/win/TrayIcon.h"
#include "frontend/win/WinUiHost.h"

#include <windows.h>
#include <shellapi.h>

#include <QTimer>

#include <utility>

namespace speecher {

struct WinFrontEnd::Native {
    Native(ApplicationController *owner,
           WinFrontEnd *q,
           std::unique_ptr<WinUiHost> winUiHost)
        : controller(owner)
        , frontEnd(q)
        , host(std::move(winUiHost))
    {
        panel = std::make_unique<DictationPanel>(controller, frontEnd);
        tray = std::make_unique<TrayIcon>(
            controller, [q] { q->showSettingsWindow(); }, frontEnd);
        trayReady = new QTimer(frontEnd);
        trayReady->setSingleShot(true);
        trayReady->setInterval(50);
        QObject::connect(trayReady, &QTimer::timeout, frontEnd, &WinFrontEnd::reportReady);
        trayReady->start();
    }

    ~Native()
    {
        setup.reset();
        settings.reset();
        tray.reset();
        panel.reset();
        host->shutdown();
    }

    win::SettingsWindow *settingsWindow()
    {
        if (!settings) {
            settings = std::make_unique<win::SettingsWindow>(controller);
            settings->setActionHook(
                [q = frontEnd](const QString &id) { q->actionTriggered(id); });
        }
        return settings.get();
    }

    ApplicationController *controller;
    WinFrontEnd *frontEnd;
    std::unique_ptr<WinUiHost> host;
    std::unique_ptr<DictationPanel> panel;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<win::SettingsWindow> settings;
    std::unique_ptr<SetupWindow> setup;
    QTimer *trayReady = nullptr;
    bool ready = false;
};

WinFrontEnd::WinFrontEnd(ApplicationController *controller,
                         std::unique_ptr<WinUiHost> host)
    : QObject(controller)
    , m_controller(controller)
    , m_native(std::make_unique<Native>(controller, this, std::move(host)))
{
    connect(m_controller->updates(), &UpdateController::openReleasePageRequested, this, [] {
        ShellExecuteW(nullptr,
                      L"open",
                      L"https://github.com/firemonster612/speecher/releases",
                      nullptr,
                      nullptr,
                      SW_SHOWNORMAL);
    });
}

WinFrontEnd::~WinFrontEnd() = default;

void WinFrontEnd::showMainWindow()
{
    showSettingsWindow();
}

void WinFrontEnd::showSettingsWindow()
{
    m_native->trayReady->stop();
    m_native->settingsWindow()->show();
    QTimer::singleShot(0, this, &WinFrontEnd::reportReady);
}

void WinFrontEnd::showSetupAssistant(SetupAssistantPage page)
{
    m_native->trayReady->stop();
    if (!m_native->setup) {
        m_native->setup = std::make_unique<SetupWindow>(
            m_controller, [this] { reportReady(); }, this);
    }
    m_native->setup->show(page);
}

bool WinFrontEnd::captureMainWindow(const QString &path)
{
    return m_native->settings && m_native->settings->capture(path);
}

void WinFrontEnd::showDictationError(const QString &message)
{
    m_native->panel->showProblem(message);
}

void WinFrontEnd::alert()
{
    MessageBeep(MB_OK);
}

void WinFrontEnd::showPanelForTest(quint64 generation)
{
    m_native->panel->showForTest(generation);
}

void WinFrontEnd::dismissPanelForTest()
{
    m_native->panel->dismissForTest();
}

bool WinFrontEnd::panelVisibleForTest() const
{
    return m_native->panel->visibleForTest();
}

quint64 WinFrontEnd::panelPresentedGenerationForTest() const
{
    return m_native->panel->presentedGenerationForTest();
}

qintptr WinFrontEnd::panelWindowStyleForTest() const
{
    return m_native->panel->windowStyleForTest();
}

void WinFrontEnd::actionTriggered(const QString &rowId)
{
    if (rowId == QStringLiteral("runSetup")) {
        m_controller->showSetupAssistant();
    } else if (rowId == QStringLiteral("checkForUpdates")) {
        if (m_controller->updates()->state() == UpdateController::State::UpdateAvailable) {
            m_controller->updates()->updateNow();
        } else {
            m_controller->updates()->checkForUpdates(m_controller->settings()->updateChannel());
        }
    } else if (rowId == QStringLiteral("whatsNew")) {
        m_controller->clearPendingWhatsNew();
    }
    // Windows UI Automation has no consent switch, so enableAccessibility is
    // deliberately a no-op.
}

void WinFrontEnd::reportReady()
{
    if (m_native->ready) {
        return;
    }
    m_native->ready = true;
    m_controller->frontEndReady();
}

} // namespace speecher
