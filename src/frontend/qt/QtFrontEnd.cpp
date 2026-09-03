#include "frontend/qt/QtFrontEnd.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "app/UpdateController.h"
#include "dictation/DictationSession.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"

#include <QApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QWidget>
#include <QWindow>
#include <QUrl>

namespace speecher {

QtFrontEnd::QtFrontEnd(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_popup(new TranscriberPopup(controller->platform()->createPopupPositioner(nullptr)))
{
    wireSessionToPopup();
    connect(controller->updates(),
            &UpdateController::changed,
            this,
            &QtFrontEnd::refreshUpdateChip);
    connect(m_popup, &TranscriberPopup::updateRequested,
            controller->updates(), &UpdateController::updateNow);
    connect(controller->updates(), &UpdateController::openReleasePageRequested, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/firemonster612/speecher/releases")));
    });
    connect(controller->session(),
            &DictationSession::stateChanged,
            this,
            &QtFrontEnd::refreshUpdateChip);
    refreshUpdateChip();
}

QtFrontEnd::~QtFrontEnd()
{
    delete m_popup;
}

void QtFrontEnd::showMainWindow()
{
    if (!m_appWindow) {
        m_appWindow = new AppWindow(m_controller);
        watchForFirstFrame(m_appWindow);
    }
    m_appWindow->show();
    m_appWindow->raise();
    m_appWindow->activateWindow();
}

void QtFrontEnd::showSettingsWindow()
{
    showMainWindow();
    m_appWindow->navigateToSettings();
}

void QtFrontEnd::showSetupAssistant(SetupAssistantPage page)
{
    if (!m_setupAssistant) {
        m_setupAssistant = new SetupAssistant(m_controller, page);
        m_setupAssistant->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_setupAssistant, &QDialog::finished, this, [this] {
            if (!m_controller->popupOnly()) {
                showMainWindow();
            }
        });
        watchForFirstFrame(m_setupAssistant);
    }
    m_setupAssistant->show();
    m_setupAssistant->raise();
    m_setupAssistant->activateWindow();
}

bool QtFrontEnd::captureMainWindow(const QString &path)
{
    return m_appWindow && m_appWindow->grab().save(path);
}

void QtFrontEnd::showDictationError(const QString &message)
{
    m_popup->showPopup(0);
    m_popup->showErrorMessage(message);
}

void QtFrontEnd::alert()
{
    QApplication::beep();
}

void QtFrontEnd::watchForFirstFrame(QWidget *window)
{
    if (m_reportedReady) {
        return;
    }
    window->installEventFilter(this);
}

// Startup work that would compete with the first frame waits until the window
// the user asked for is actually on screen. Which of exposure and painting
// comes first is up to the platform, so both count.
bool QtFrontEnd::eventFilter(QObject *watched, QEvent *event)
{
    if (m_reportedReady) {
        return QObject::eventFilter(watched, event);
    }
    bool onScreen = false;
    switch (event->type()) {
    case QEvent::Show:
        // The native window exists by now, and only it sees exposure.
        if (auto *widget = qobject_cast<QWidget *>(watched)) {
            if (QWindow *handle = widget->windowHandle()) {
                handle->installEventFilter(this);
            }
        }
        break;
    case QEvent::Expose:
        if (const auto *handle = qobject_cast<QWindow *>(watched)) {
            onScreen = handle->isExposed();
        }
        break;
    case QEvent::Paint:
        if (const auto *widget = qobject_cast<QWidget *>(watched)) {
            onScreen = widget->isWindow() && widget->isVisible();
        }
        break;
    default:
        break;
    }
    if (onScreen) {
        m_reportedReady = true;
        m_controller->frontEndReady();
    }
    return QObject::eventFilter(watched, event);
}

void QtFrontEnd::wireSessionToPopup()
{
    DictationSession *session = m_controller->session();
    connect(session, &DictationSession::previewDisplayChanged, m_popup, &TranscriberPopup::setPreview);
    connect(session, &DictationSession::audioLevelChanged, m_popup, &TranscriberPopup::setLevel);
    connect(session, &DictationSession::popupStatusChanged, m_popup, &TranscriberPopup::setStatus);
    connect(session, &DictationSession::popupShowRequested, m_popup, &TranscriberPopup::showPopup);
    connect(m_popup, &TranscriberPopup::popupPresented, session, &DictationSession::popupPresented);
    connect(session, &DictationSession::popupHideRequested, m_popup, &TranscriberPopup::hide);
    connect(session, &DictationSession::popupFrozenChanged, m_popup, &TranscriberPopup::setFrozen);
    connect(session, &DictationSession::popupRefiningChanged, m_popup, &TranscriberPopup::setRefining);
    connect(session, &DictationSession::popupOAuthRefreshRequested, m_popup, &TranscriberPopup::showOAuthRefreshIndicator);
    connect(session, &DictationSession::popupListeningIndicatorRequested, m_popup, &TranscriberPopup::showListeningIndicator);
    connect(session, &DictationSession::popupMessageRequested, m_popup, &TranscriberPopup::showMessage);
    connect(session, &DictationSession::popupErrorRequested, m_popup, &TranscriberPopup::showErrorMessage);
    connect(m_popup, &TranscriberPopup::errorDismissed, session, [session] {
        if (session->state() == DictationState::Error) {
            session->stopListening();
        }
    });
    connect(m_popup, &TranscriberPopup::enableAccessibilityRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_popup->showAccessibilityError(error);
        }
    });
    connect(m_controller,
            &ApplicationController::accessibilityStateChanged,
            m_popup,
            &TranscriberPopup::setAccessibilityState);
}

void QtFrontEnd::refreshUpdateChip()
{
    UpdateController *updates = m_controller->updates();
    const DictationState sessionState = m_controller->session()->state();
    const bool canAct = sessionState == DictationState::Idle
        || sessionState == DictationState::Error;
    switch (updates->state()) {
    case UpdateController::State::UpdateAvailable:
        m_popup->setUpdateChip(QStringLiteral("Update available"), true, canAct);
        break;
    case UpdateController::State::Downloading:
        m_popup->setUpdateChip(
            QStringLiteral("Downloading %1%").arg(updates->downloadPercent()), true, false);
        break;
    case UpdateController::State::ReadyToRestart:
        m_popup->setUpdateChip(updates->errorMessage().isEmpty()
                                   ? QStringLiteral("Restart to finish updating")
                                   : updates->errorMessage(),
                               true,
                               canAct);
        break;
    case UpdateController::State::RestartPending:
        m_popup->setUpdateChip(
            QStringLiteral("Restarting after this dictation…"), true, false);
        break;
    case UpdateController::State::Error:
        m_popup->setUpdateChip(updates->errorMessage(), true, canAct);
        break;
    default:
        m_popup->setUpdateChip({}, false, false);
        break;
    }
}

} // namespace speecher
