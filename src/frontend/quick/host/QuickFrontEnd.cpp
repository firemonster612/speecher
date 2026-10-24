#include "frontend/quick/host/QuickFrontEnd.h"

#include "app/ApplicationController.h"
#include "app/PhoneTransfer.h"
#include "app/PlatformComposition.h"
#include "app/UpdateController.h"
#include "dictation/DictationSession.h"
#include "frontend/quick/host/QuickPopupWindow.h"

#include <QApplication>
#include <QDebug>
#include <QDirIterator>
#include <QEvent>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlExtensionPlugin>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QTimer>

#include <utility>

// The UI's QML modules are linked in statically (cmake/QuickUi.cmake).
Q_IMPORT_QML_PLUGIN(Speecher_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Speecher_LaunchPlugin)
Q_IMPORT_QML_PLUGIN(Speecher_DemoPlugin)
Q_IMPORT_QML_PLUGIN(Speecher_WordingPlugin)
Q_IMPORT_QML_PLUGIN(Speecher_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Speecher_BackendPlugin)
Q_IMPORT_QML_PLUGIN(Speecher_AppPlugin)
Q_IMPORT_QML_PLUGIN(Speecher_PopupPlugin)

namespace speecher {

namespace {

const QString appModule = QStringLiteral("Speecher.App");
// Long enough for a page that has just loaded to settle.
constexpr int grabDelayMs = 2000;

// Atkinson Hyperlegible Next and the Lucide icon font, which Theme names.
void registerFonts()
{
    QDirIterator fonts(QStringLiteral(":/speecher/assets"), {QStringLiteral("*.ttf")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (fonts.hasNext()) {
        const QString path = fonts.next();
        if (QFontDatabase::addApplicationFont(path) < 0) {
            qWarning().noquote() << "Could not load the UI font" << path;
        }
    }
}

// A test hook: SPEECHER_GRAB_MAIN_WINDOW=<file.png> saves the main window a
// moment after each time it is shown. A headless compositor lets no other
// client take a screenshot, so this is how the real window is checked.
void grabWhenAsked(QQuickWindow *window)
{
    const QString path = qEnvironmentVariable("SPEECHER_GRAB_MAIN_WINDOW");
    if (path.isEmpty()) {
        return;
    }
    QTimer::singleShot(grabDelayMs, window, [window, path] {
        if (!window->isVisible()) {
            return;
        }
        if (!window->grabWindow().save(path)) {
            qWarning().noquote() << "Could not save the main window to" << path;
        }
    });
}

// image://qr/<text in base64>: the text as a QR code from core's
// qrCodeImage, at most as large as asked for, for QrCode.qml.
class QrCodeImageProvider final : public QQuickImageProvider {
public:
    QrCodeImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        // Big enough to scan when nothing asks for a size.
        constexpr int defaultSide = 512;
        const QString text = QString::fromUtf8(QByteArray::fromBase64(id.toLatin1()));
        const QImage image = qrCodeImage(text, requestedSize.width() > 0 ? requestedSize.width() : defaultSide);
        if (size) {
            *size = image.size();
        }
        return image;
    }
};

} // namespace

QuickFrontEnd::QuickFrontEnd(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_settings(controller)
    , m_status(controller)
    , m_dictation(controller)
    , m_updates(controller, controller->updates())
    , m_transcription(controller)
    , m_system(controller)
{
    registerFonts();
    m_engine.addImageProvider(QStringLiteral("qr"), new QrCodeImageProvider);
    // Into the log with the rest, marked so a load's warnings are easy to find.
    m_engine.setOutputWarningsToStandardError(false);
    connect(&m_engine, &QQmlEngine::warnings, this, [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings) {
            qWarning().noquote() << "QML:" << warning.toString();
        }
    });
    QQmlComponent themeSetting(&m_engine, appModule, QStringLiteral("ThemeSetting"));
    m_themeSetting = themeSetting.create();
    if (!m_themeSetting) {
        qWarning().noquote() << "QML: could not follow the Theme setting:" << themeSetting.errorString();
    }
    m_popup = new QuickPopupWindow(&m_engine, controller->platform()->createPopupPositioner(nullptr), this);
    DictationSession *session = controller->session();
    connect(session, &DictationSession::popupShowRequested, m_popup, &QuickPopupWindow::show);
    connect(m_popup, &QuickPopupWindow::presented, session, &DictationSession::popupPresented);
    connect(&m_dictation, &QuickDictation::pageRequested, this, &QuickFrontEnd::showPage);
    connect(&m_updates, &QuickUpdates::pageRequested, this, &QuickFrontEnd::showPage);
    // An update restart brings back the windows that were on screen, never
    // the microphone: the new process has no dictation gesture behind it.
    controller->updates()->setRestoreStateProvider([this] {
        return m_main && m_main->isVisible() ? QStringLiteral("settings") : QString();
    });
    controller->setFrontEnd(this);
}

QuickFrontEnd::~QuickFrontEnd()
{
    m_controller->setFrontEnd(nullptr);
    // The updater outlives this object and its windows.
    m_controller->updates()->setRestoreStateProvider({});
    // Going away is not the person closing setup.
    if (m_setup) {
        m_setup->disconnect(this);
    }
    // Before the engine their bindings run in.
    delete m_main;
    delete m_setup;
    delete m_popup;
    delete m_themeSetting;
}

void QuickFrontEnd::showMainWindow()
{
    // After an update, Main first opens on its news.
    showPage(!m_main && m_updates.whatsNewPending() ? QStringLiteral("updates") : QString());
}

void QuickFrontEnd::showPage(const QString &pageId)
{
    // Before a first Main is made, so it builds only the page asked for:
    // building Updates on the way to another page would mark the news seen.
    if (!pageId.isEmpty()) {
        setPage(pageId);
    }
    if (!m_main) {
        m_main = createWindow(QStringLiteral("Main"));
        if (!m_main) {
            return;
        }
        // Startup work that would compete with the first paint waits for it.
        connect(m_main, &QQuickWindow::frameSwapped, m_controller, &ApplicationController::frontEndReady,
                Qt::SingleShotConnection);
    }
    m_main->show();
    m_main->raise();
    m_main->requestActivate();
    grabWhenAsked(m_main);
}

void QuickFrontEnd::hideMainWindow()
{
    if (m_main) {
        m_main->hide();
    }
}

void QuickFrontEnd::showSettingsWindow()
{
    showMainWindow();
}

void QuickFrontEnd::showSetupAssistant(SetupAssistantPage)
{
    // Nothing asks for one page yet, so the assistant always starts at its
    // first step.
    if (!m_setup) {
        m_setup = createWindow(QStringLiteral("SetupAssistant"));
        if (!m_setup) {
            return;
        }
        connect(m_setup, SIGNAL(finished()), this, SLOT(finishSetup()));
        connect(m_setup, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible) {
                setupClosed();
            }
        });
    }
    m_setup->show();
    m_setup->raise();
    m_setup->requestActivate();
}

void QuickFrontEnd::showTranscribeFiles(const QStringList &paths)
{
    m_transcription.addFiles(paths);
    showPage(QStringLiteral("transcribe"));
}

void QuickFrontEnd::showDictationError(const QString &message, const PopupErrorAction &fix)
{
    m_dictation.showError(message, fix);
    m_popup->show(0);
}

void QuickFrontEnd::alert()
{
    QApplication::beep();
}

void QuickFrontEnd::notifyIfNoWindowShown(const QString &title, const QString &message, const QString &pageId)
{
    qInfo().noquote() << "Notification, not shown yet:" << title << "-" << message << "(page" << pageId + ")";
}

void QuickFrontEnd::finishSetup()
{
    m_controller->completeSetup();
}

QQuickWindow *QuickFrontEnd::createWindow(const QString &typeName)
{
    QQmlComponent component(&m_engine, appModule, typeName);
    QObject *object = component.create();
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning().noquote() << "QML: could not open" << typeName + ":" << component.errorString();
        delete object;
        return nullptr;
    }
    window->installEventFilter(this);
    return window;
}

bool QuickFrontEnd::eventFilter(QObject *watched, QEvent *event)
{
    // Closing hides rather than destroys the native window: Qt's Wayland
    // window can be freed under a frame the render thread is still ending.
    if (event->type() == QEvent::Close) {
        event->ignore();
        static_cast<QQuickWindow *>(watched)->hide();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void QuickFrontEnd::setPage(const QString &pageId)
{
    if (QObject *navigation = m_engine.singletonInstance<QObject *>(appModule, QStringLiteral("Navigation"))) {
        navigation->setProperty("current", pageId);
    }
}

void QuickFrontEnd::setupClosed()
{
    std::exchange(m_setup, nullptr)->deleteLater();
    m_controller->setupAssistantClosed();
}

} // namespace speecher
