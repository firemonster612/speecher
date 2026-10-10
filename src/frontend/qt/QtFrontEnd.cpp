#include "frontend/qt/QtFrontEnd.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PlatformComposition.h"
#include "app/UpdateBanner.h"
#include "app/UpdateController.h"
#include "dictation/DictationSession.h"
#include "ui/AppWindow.h"
#include "ui/HomePage.h"
#include "ui/SetupAssistant.h"
#include "ui/TranscribePage.h"
#include "ui/TranscribeWindow.h"
#include "ui/TranscriberPopup.h"

#ifdef Q_OS_LINUX
#include "frontend/qt/LinuxAuthPrompt.h"
#include "frontend/qt/LinuxTrayIcon.h"
#endif

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QThread>
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
    m_tray = new LinuxTrayIcon(controller, this);
    installLinuxAuthPrompt();
#endif
    wireSessionToPopup();
    connect(controller->updateBanner(),
            &UpdateBanner::changed,
            this,
            &QtFrontEnd::refreshUpdateChip);
    connect(m_popup, &TranscriberPopup::updateRequested,
            controller->updateBanner(), &UpdateBanner::runAction);
    connect(m_popup, &TranscriberPopup::updateLaterRequested,
            controller->updateBanner(), &UpdateBanner::later);
    connect(m_popup, &TranscriberPopup::updateDismissRequested,
            controller->updateBanner(), &UpdateBanner::dismiss);
    connect(controller, &ApplicationController::whatsNewChanged,
            this, &QtFrontEnd::refreshWhatsNewChip);
    connect(m_popup, &TranscriberPopup::whatsNewRequested, this, [this] {
        showMainWindow();
        m_appWindow->showWhatsNew();
    });
    connect(m_popup, &TranscriberPopup::whatsNewDismissed,
            controller, &ApplicationController::clearPendingWhatsNew);
    // On-screen windows only, never the microphone: an update restart must not
    // reopen a recording the new process was never asked for. The
    // restart already waits for the session to finish; resuming what it waited
    // out would record with no dictation gesture behind it.
    controller->updates()->setRestoreStateProvider([this] {
        QStringList state;
        if (m_appWindow && m_appWindow->isVisible()) {
            state.append(QStringLiteral("settings"));
        }
        return state.join(QLatin1Char(','));
    });
    connect(controller->updates(), &UpdateController::openReleasePageRequested, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/firemonster612/speecher/releases")));
    });
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
    delete m_appWindow;
    delete m_popup;
    delete m_transcribeWindow;
}

void QtFrontEnd::showMainWindow()
{
    if (!m_appWindow) {
        m_appWindow = new AppWindow(m_controller);
        watchForFirstFrame(m_appWindow);
    }
    // Opened from hidden it starts on Home; one already up keeps its page.
    if (!m_appWindow->isVisible()) {
        m_appWindow->showHome();
    }
    m_appWindow->show();
    m_appWindow->raise();
    m_appWindow->activateWindow();
}

void QtFrontEnd::hideMainWindow()
{
    if (m_appWindow) {
        m_appWindow->hide();
    }
}

void QtFrontEnd::showSettingsWindow()
{
    showMainWindow();
    m_appWindow->showPage(QStringLiteral("general"));
}

// Opened files get the compact window, not the main one; the Transcribe page
// in the main window's sidebar stays for people who go there themselves.
void QtFrontEnd::showTranscribeFiles(const QStringList &paths)
{
    TranscribeWindow *window = transcribeWindow();
    window->page()->addFiles(paths);
    window->show();
    window->raise();
    window->activateWindow();
}

TranscribeWindow *QtFrontEnd::transcribeWindow()
{
    if (!m_transcribeWindow) {
        m_transcribeWindow = new TranscribeWindow(m_controller);
        // The compact window has no settings of its own; they open in the
        // main window.
        connect(m_transcribeWindow->page(), &TranscribePage::pageRequested, this,
                [this](const QString &pageId) {
                    showMainWindow();
                    m_appWindow->showPage(pageId);
                });
        watchForFirstFrame(m_transcribeWindow);
    }
    return m_transcribeWindow;
}

void QtFrontEnd::showSetupAssistant(SetupAssistantPage page)
{
    if (!m_setupAssistant) {
        m_setupAssistant = new SetupAssistant(m_controller, page);
        m_setupAssistant->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_setupAssistant, &QDialog::finished,
                m_controller, &ApplicationController::setupAssistantClosed);
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
    // Screenshot automation: SPEECHER_GRAB_PAGE names a page id, as every
    // front end takes it ("general", "vocabulary:corrections"; see
    // resolvePage), to show before the grab. "transcribe-window" grabs the
    // compact Transcribe window instead. Unset leaves the window as launched.
    const QString requested = qEnvironmentVariable("SPEECHER_GRAB_PAGE");
    const QStringList request = requested.toLower().split(u':');
    // "setup" or "setup:<page title>" grabs the setup assistant instead,
    // advanced to the first page whose title matches (e.g. "setup:refinement").
    if (request.first() == QStringLiteral("setup")) {
        // SPEECHER_GRAB_DOWNLOAD names a Local Model to start downloading
        // first, so later pages show a download in flight.
        if (const LocalModel *model = findLocalModel(qEnvironmentVariable("SPEECHER_GRAB_DOWNLOAD"))) {
            m_controller->localSetup()->download(*model);
        }
        auto *assistant = new SetupAssistant(m_controller);
        const QStringList assistantSize = qEnvironmentVariable("SPEECHER_GRAB_SIZE").split(u'x');
        if (assistantSize.size() == 2) {
            assistant->resize(assistantSize.at(0).toInt(), assistantSize.at(1).toInt());
        }
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
        if (!clickGrabButtons(assistant)) {
            assistant->deleteLater();
            return false;
        }
        // The credential probes run off-thread and answer through queued
        // signals; each answer changes a label and posts a relayout. One
        // processEvents pass grabs mid-update, with statuses painted at the
        // previous text's width. Let the page settle first.
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(25);
        }
        const bool saved = assistant->grab().save(path);
        assistant->deleteLater();
        return saved;
    }
    // "stats-image" saves the picture Home's Share menu copies, read back
    // from the clipboard.
    if (request.first() == QStringLiteral("stats-image")) {
        m_appWindow->showPage(QStringLiteral("home"));
        QCoreApplication::processEvents();
        auto *home = m_appWindow->findChild<HomePage *>();
        if (!home) {
            return false;
        }
        home->copyStatsImage();
        return QGuiApplication::clipboard()->image().save(path);
    }
    QWidget *target = m_appWindow;
    if (request.first() == QStringLiteral("transcribe-window")) {
        showTranscribeFiles({});
        QCoreApplication::processEvents();
        target = m_transcribeWindow;
    }
    // SPEECHER_GRAB_SIZE=WxH resizes the window first.
    const QStringList size = qEnvironmentVariable("SPEECHER_GRAB_SIZE").split(u'x');
    if (size.size() == 2) {
        target->resize(size.at(0).toInt(), size.at(1).toInt());
    }
    if (!requested.isEmpty() && target == m_appWindow) {
        m_appWindow->showPage(requested);
        QCoreApplication::processEvents();
    }
    // Height-for-width rows (wrapped labels, the heatmap) settle over a few
    // posted relayouts; one processEvents pass grabs them half laid out.
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 300) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    // SPEECHER_GRAB_SCROLL=<pixels> or =bottom scrolls the visible page, so
    // a second grab can show what lies below the first screen.
    const QString scroll = qEnvironmentVariable("SPEECHER_GRAB_SCROLL");
    if (!scroll.isEmpty()) {
        for (QScrollArea *area : m_appWindow->findChildren<QScrollArea *>()) {
            if (area->isVisible()) {
                QScrollBar *bar = area->verticalScrollBar();
                bar->setValue(scroll == QStringLiteral("bottom") ? bar->maximum() : scroll.toInt());
            }
        }
        QCoreApplication::processEvents();
    }
    // SPEECHER_GRAB_CLICK names buttons (by objectName, comma-separated) to
    // click once the page is up, so a grab can show what an interaction
    // leaves behind. SPEECHER_GRAB_WAIT_MS lets what they started run first.
    if (!clickGrabButtons(target)) {
        return false;
    }
    // A click that opened a dialog grabs the dialog.
    if (QWidget *dialog = QApplication::activeModalWidget(); dialog && dialog != target) {
        target = dialog;
    }
    return target->grab().save(path);
}

bool QtFrontEnd::clickGrabButtons(QWidget *window)
{
    const QStringList clicks = qEnvironmentVariable("SPEECHER_GRAB_CLICK").split(u',', Qt::SkipEmptyParts);
    for (const QString &click : clicks) {
        QCoreApplication::processEvents();
        auto *button = window->findChild<QAbstractButton *>(click);
        if (!button) {
            qWarning("SPEECHER_GRAB_CLICK names no button: %s", qPrintable(click));
            return false;
        }
        button->click();
    }
    QElapsedTimer settle;
    settle.start();
    const int waitMs = qEnvironmentVariableIntValue("SPEECHER_GRAB_WAIT_MS");
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    } while (settle.elapsed() < waitMs);
    return true;
}

void QtFrontEnd::showDictationError(const QString &message, const PopupErrorAction &fix)
{
    m_popup->showPopup(0);
    showPopupError(message, fix);
}

// Linux has no system permission panels to open, so only a settings page
// becomes a button.
static QString fixButtonLabel(const PopupErrorAction &fix)
{
    return fix.fix == ErrorFix::SettingsPage ? popupErrorActionLabel(fix) : QString();
}

void QtFrontEnd::showPopupError(const QString &message, const PopupErrorAction &fix)
{
    m_errorFix = fix;
    m_popup->showErrorMessage(message, fixButtonLabel(fix));
}

// An outcome can offer a fix too, such as a sign-in that expired while a
// fallback did the work; its button opens the page an error's would.
void QtFrontEnd::showPopupOutcome(const QString &message, PopupOutcome outcome, const PopupErrorAction &fix)
{
    m_errorFix = fix;
    m_popup->showMessage(message, outcome, fixButtonLabel(fix));
}

void QtFrontEnd::applyErrorFix()
{
    if (m_errorFix.fix != ErrorFix::SettingsPage) {
        return;
    }
    showMainWindow();
    m_appWindow->showPage(m_errorFix.pageId);
}

void QtFrontEnd::alert()
{
    QApplication::beep();
}

void QtFrontEnd::notifyIfNoWindowShown(const QString &title, const QString &message, const QString &pageId)
{
    if ((m_appWindow && m_appWindow->isVisible()) || (m_setupAssistant && m_setupAssistant->isVisible())) {
        return;
    }
#ifdef Q_OS_LINUX
    m_tray->showMessage(title, message, [this, pageId] {
        showMainWindow();
        m_appWindow->showPage(pageId);
    });
#else
    Q_UNUSED(title);
    Q_UNUSED(message);
    Q_UNUSED(pageId);
#endif
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
    connect(session, &DictationSession::stateChanged, m_popup,
            [this, session] { m_popup->setSessionState(session->state()); });
    connect(session, &DictationSession::popupShowRequested, m_popup, &TranscriberPopup::showPopup);
    connect(m_popup, &TranscriberPopup::popupPresented, session, &DictationSession::popupPresented);
    connect(session, &DictationSession::popupHideRequested, m_popup, &TranscriberPopup::hide);
    connect(session, &DictationSession::popupFrozenChanged, m_popup, &TranscriberPopup::setFrozen);
    connect(session, &DictationSession::popupRefiningChanged, m_popup, &TranscriberPopup::setRefining);
    connect(session, &DictationSession::popupRefinementPreviewChanged, m_popup, &TranscriberPopup::setRefinementPreview);
    connect(session, &DictationSession::popupOAuthRefreshRequested, m_popup, &TranscriberPopup::showOAuthRefreshIndicator);
    connect(session, &DictationSession::popupListeningIndicatorRequested, m_popup, &TranscriberPopup::showListeningIndicator);
    connect(session, &DictationSession::popupMessageRequested, this, &QtFrontEnd::showPopupOutcome);
    connect(session, &DictationSession::popupErrorRequested, this, &QtFrontEnd::showPopupError);
    connect(m_popup, &TranscriberPopup::errorActionRequested, this, &QtFrontEnd::applyErrorFix);
    connect(m_popup, &TranscriberPopup::pauseToggled, session, &DictationSession::togglePause);
    connect(m_popup, &TranscriberPopup::cancelRequested, m_controller, &ApplicationController::cancel);
    connect(session, &DictationSession::popupSelectionEditReviewRequested, m_popup,
            &TranscriberPopup::showSelectionEditReview);
    connect(session, &DictationSession::popupSelectionEditReviewEnded, m_popup,
            &TranscriberPopup::hideSelectionEditReview);
    connect(m_popup, &TranscriberPopup::keepOriginalRequested, m_controller, &ApplicationController::cancel);
    connect(m_popup, &TranscriberPopup::replaceSelectionRequested, session, &DictationSession::replaceSelection);
    connect(m_popup, &TranscriberPopup::errorDismissed, session, [session] {
        if (session->state() == DictationState::Error) {
            session->stopListening();
        }
    });
}

void QtFrontEnd::refreshUpdateChip()
{
    UpdateBannerModel banner = m_controller->updateBanner()->model();
    banner.visible = banner.visible && banner.showInPopup;
    m_popup->setUpdateBanner(banner);
}

void QtFrontEnd::refreshWhatsNewChip()
{
    m_popup->setWhatsNewBanner(whatsNewBanner(m_controller->updates()->currentVersion()),
                               !m_controller->pendingWhatsNewVersion().isEmpty());
}

} // namespace speecher
