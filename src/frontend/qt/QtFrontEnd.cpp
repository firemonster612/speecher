#include "frontend/qt/QtFrontEnd.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "app/UpdateController.h"
#include "dictation/DictationSession.h"
#include "ui/AppPage.h"
#include "ui/AppWindow.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscriberPopup.h"

#ifdef Q_OS_LINUX
#include "frontend/qt/LinuxTrayIcon.h"
#endif

#include <QApplication>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QCoreApplication>
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
#ifdef Q_OS_LINUX
    // Like the mac menu bar extra, the tray icon exists from launch: in
    // daemon mode it is the only sign the process is running and the global
    // shortcut has something to reach.
    new LinuxTrayIcon(controller, this);
#endif
    wireSessionToPopup();
    connect(controller->updates(),
            &UpdateController::changed,
            this,
            &QtFrontEnd::refreshUpdateChip);
    connect(m_popup, &TranscriberPopup::updateRequested,
            controller->updates(), &UpdateController::installAndRestart);
    connect(controller, &ApplicationController::whatsNewChanged,
            this, &QtFrontEnd::refreshWhatsNewChip);
    connect(m_popup, &TranscriberPopup::whatsNewRequested, this, [this] {
        showMainWindow();
        m_appWindow->showWhatsNew();
    });
    connect(m_popup, &TranscriberPopup::whatsNewDismissed,
            controller, &ApplicationController::clearPendingWhatsNew);
    controller->updates()->setRestoreStateProvider([this, controller] {
        QStringList state;
        const DictationState sessionState = controller->session()->state();
        if (sessionState != DictationState::Idle && sessionState != DictationState::Error) {
            state.append(QStringLiteral("listening"));
        }
        if (m_appWindow && m_appWindow->isVisible()) {
            state.append(QStringLiteral("settings"));
        }
        return state.join(QLatin1Char(','));
    });
    connect(controller->updates(), &UpdateController::openReleasePageRequested, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/firemonster612/speecher/releases")));
    });
    connect(controller->session(),
            &DictationSession::stateChanged,
            this,
            &QtFrontEnd::refreshUpdateChip);
    // Each popup re-derives the What's New chip: the offer auto-hides per
    // appearance but returns on the next dictation until it is dismissed, the
    // same as the mac and Windows panels.
    connect(controller->session(),
            &DictationSession::popupShowRequested,
            this,
            &QtFrontEnd::refreshWhatsNewChip);
    refreshUpdateChip();
    refreshWhatsNewChip();

    // SPEECHER_POPUP_CAPTURE_DIR: capture seam mirroring the mac setup
    // assistant's; saves numbered frames of the dictation popup while it is
    // visible so a headless end-to-end run can be assembled into a video.
    const QString captureDir = qEnvironmentVariable("SPEECHER_POPUP_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        auto *capture = new QTimer(this);
        capture->setInterval(66);
        connect(capture, &QTimer::timeout, this, [this, captureDir] {
            static int frame = 0;
            if (m_popup->isVisible()) {
                m_popup->grab().save(QStringLiteral("%1/frame-%2.png")
                                         .arg(captureDir)
                                         .arg(++frame, 6, 10, QLatin1Char('0')));
            }
        });
        capture->start();
    }
}

QtFrontEnd::~QtFrontEnd()
{
    // The updater outlives the front end, so drop the provider that reaches
    // back into this object and its windows.
    m_controller->updates()->setRestoreStateProvider({});
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
    if (!m_appWindow) {
        return false;
    }
    // Screenshot automation: SPEECHER_GRAB_PAGE names a settings page
    // (general, audio, output, auth, refinement, vocabulary), optionally with
    // a tab index ("vocabulary:2"), to show before the grab. Unset or unknown
    // leaves the window as launched.
    static const QStringList pageNames{
        QStringLiteral("general"), QStringLiteral("audio"), QStringLiteral("output"),
        QStringLiteral("auth"), QStringLiteral("refinement"), QStringLiteral("vocabulary")};
    const QStringList request = qEnvironmentVariable("SPEECHER_GRAB_PAGE").toLower().split(u':');
    // "setup" or "setup:<page title>" grabs the setup assistant instead,
    // advanced to the first page whose title matches (e.g. "setup:refinement").
    if (request.first() == QStringLiteral("setup")) {
        auto *assistant = new SetupAssistant(m_controller);
        const QStringList titles = assistant->pageTitles();
        const QString wanted = request.value(1);
        int target = 0;
        if (!wanted.isEmpty()) {
            // Resolve the page before walking: stepping past a typoed title
            // would fire every page's side effects and grab the last page.
            for (target = 0; target < titles.size(); ++target) {
                if (titles.at(target).toLower() == wanted) {
                    break;
                }
            }
            if (target == titles.size()) {
                assistant->deleteLater();
                return false;
            }
        }
        assistant->show();
        for (int i = 0; i < target; ++i) {
            assistant->next();
        }
        QCoreApplication::processEvents();
        const bool saved = assistant->grab().save(path);
        assistant->deleteLater();
        return saved;
    }
    const int page = pageNames.indexOf(request.first());
    // SPEECHER_GRAB_SIZE=WxH resizes the window first.
    const QStringList size = qEnvironmentVariable("SPEECHER_GRAB_SIZE").split(u'x');
    if (size.size() == 2) {
        m_appWindow->resize(size.at(0).toInt(), size.at(1).toInt());
    }
    if (request.first() == QStringLiteral("whatsnew")) {
        m_appWindow->showWhatsNew();
        QCoreApplication::processEvents();
    }
    if (page >= 0) {
        m_appWindow->navigateToSettings(static_cast<AppPageId>(page));
        if (request.size() > 1) {
            for (QTabWidget *tabs : m_appWindow->findChildren<QTabWidget *>()) {
                if (tabs->isVisible()) {
                    tabs->setCurrentIndex(request.at(1).toInt());
                }
            }
        }
        QCoreApplication::processEvents();
    }
    // SPEECHER_GRAB_CLICK names a button (by objectName) to click once the
    // page is up, so a grab can show what an interaction leaves behind.
    const QString click = qEnvironmentVariable("SPEECHER_GRAB_CLICK");
    if (!click.isEmpty()) {
        auto *button = m_appWindow->findChild<QPushButton *>(click);
        if (!button) {
            qWarning("SPEECHER_GRAB_CLICK names no button: %s", qPrintable(click));
            return false;
        }
        button->click();
        QCoreApplication::processEvents();
    }
    return m_appWindow->grab().save(path);
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
    connect(session, &DictationSession::popupRefinementPreviewChanged, m_popup, &TranscriberPopup::setRefinementPreview);
    connect(session, &DictationSession::popupOAuthRefreshRequested, m_popup, &TranscriberPopup::showOAuthRefreshIndicator);
    connect(session, &DictationSession::popupListeningIndicatorRequested, m_popup, &TranscriberPopup::showListeningIndicator);
    connect(session, &DictationSession::popupMessageRequested, m_popup, &TranscriberPopup::showMessage);
    connect(session, &DictationSession::popupErrorRequested, m_popup, &TranscriberPopup::showErrorMessage);
    connect(m_popup, &TranscriberPopup::errorDismissed, session, [session] {
        if (session->state() == DictationState::Error) {
            session->stopListening();
        }
    });
}

void QtFrontEnd::refreshUpdateChip()
{
    UpdateController *updates = m_controller->updates();
    switch (updates->state()) {
    case UpdateController::State::UpdateAvailable:
        // Base version only: a full nightly identifier would stretch the
        // banner across the screen. Clicking during a dictation is safe: the
        // restart parks until the session is idle and the relaunch restores
        // what was on screen.
        m_popup->setUpdateBanner(
            QStringLiteral("Speecher %1 available")
                .arg(updates->availableVersion().section(QLatin1Char('-'), 0, 0)),
            QStringLiteral("Install and restart"),
            true);
        break;
    case UpdateController::State::Downloading:
        m_popup->setUpdateBanner(
            QStringLiteral("Downloading %1%").arg(updates->downloadPercent()), {}, false);
        break;
    case UpdateController::State::ReadyToRestart:
        m_popup->setUpdateBanner(updates->errorMessage().isEmpty()
                                     ? QStringLiteral("Update ready")
                                     : updates->errorMessage(),
                                 QStringLiteral("Restart now"),
                                 true);
        break;
    case UpdateController::State::RestartPending:
        m_popup->setUpdateBanner(
            QStringLiteral("Restarting after this dictation…"), {}, false);
        break;
    case UpdateController::State::Restarting:
        m_popup->setUpdateBanner(QStringLiteral("Restarting…"), {}, false);
        break;
    case UpdateController::State::Error:
        m_popup->setUpdateBanner(updates->errorMessage(),
                                 updates->manualInstallRequired()
                                     ? QStringLiteral("Open release page")
                                     : QStringLiteral("Try again"),
                                 true);
        break;
    case UpdateController::State::CheckFailed:
        if (updates->repeatedAutomaticCheckFailure()) {
            m_popup->setUpdateBanner(QStringLiteral("Update check failed"),
                                     QStringLiteral("Try again"),
                                     true);
        } else {
            m_popup->setUpdateBanner({}, {}, false);
        }
        break;
    default:
        m_popup->setUpdateBanner({}, {}, false);
        break;
    }
}

void QtFrontEnd::refreshWhatsNewChip()
{
    if (m_controller->pendingWhatsNewVersion().isEmpty()) {
        m_popup->setWhatsNewBanner({}, false);
        return;
    }
    m_popup->setWhatsNewBanner(
        QStringLiteral("Speecher %1 installed")
            .arg(m_controller->updates()->currentVersion().section(QLatin1Char('-'), 0, 0)),
        true);
}

} // namespace speecher
