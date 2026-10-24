#include "frontend/quick/host/QuickSystem.h"

#include "app/ApplicationController.h"
#include "app/PhoneTransfer.h"
#include "core/SettingsStore.h"
#include "platform/LinuxDesktopIntegration.h"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QSysInfo>
#include <QUrl>

#include <utility>

namespace speecher {

namespace {

QuickSystem *g_instance = nullptr;

using Operation = VirtualKeyboardSetup::Operation;
using Test = VirtualKeyboardSetup::Test;

QVariantMap checkEntry(const char *id, const char *state, const char *reason, const char *fix = "")
{
    return {{QStringLiteral("id"), QLatin1String(id)},
            {QStringLiteral("state"), QLatin1String(state)},
            {QStringLiteral("reason"), QLatin1String(reason)},
            {QStringLiteral("fix"), QLatin1String(fix)}};
}

// The microphone needs no grant: the Linux desktop lets every app record.
QVariantMap microphoneCheck()
{
    return checkEntry("microphoneAccess", "ok", "notAsked");
}

QVariantMap accessibilityCheck(const ApplicationController *controller)
{
    if (!controller->accessibilitySupported()) {
        return checkEntry("desktopAccessibility", "off", "unsupported");
    }
    if (!controller->accessibilityEnabled()) {
        return checkEntry("desktopAccessibility", "off", "off", "enable");
    }
    if (!controller->accessibilityPersistent()) {
        return checkEntry("desktopAccessibility", "attention", "sessionOnly", "enable");
    }
    return checkEntry("desktopAccessibility", "ok", "on");
}

QVariantMap shortcutCheck(const ApplicationController *controller)
{
    if (!controller->globalShortcutSupportKnown()) {
        return checkEntry("globalShortcuts", "working", "checking");
    }
    if (!controller->globalShortcutsSupported()) {
        return checkEntry("globalShortcuts", "off", "unsupported");
    }
    if (controller->globalShortcutDisplay().isEmpty()) {
        return checkEntry("globalShortcuts", "attention", "notSet");
    }
    return checkEntry("globalShortcuts", "ok", "registered");
}

QVariantMap virtualKeyboardCheck(const VirtualKeyboardSetup &keyboard)
{
    const char *id = "virtualKeyboardHealth";
    switch (keyboard.operation()) {
    case Operation::SettingUp:
        return checkEntry(id, "working", "settingUp");
    case Operation::Starting:
        return checkEntry(id, "working", "starting");
    case Operation::Removing:
        return checkEntry(id, "working", "removing");
    case Operation::None:
        break;
    }
    if (keyboard.test() != Test::Off) {
        return checkEntry(id, "attention", "testing");
    }
    const YdotoolSetupStatus status = keyboard.status();
    switch (status.state) {
    case YdotoolSetupState::Disabled:
        return status.speecherManagedSetupInstalled ? checkEntry(id, "attention", "disabled", "enable")
                                                    : checkEntry(id, "attention", "availableOutside", "setUp");
    case YdotoolSetupState::NotInstalled:
        return checkEntry(id, "attention", "notInstalled", "setUp");
    case YdotoolSetupState::NeedsUinputPermission:
        return checkEntry(id, "attention", "needsUinputPermission", "setUp");
    case YdotoolSetupState::NeedsSignOut:
        return checkEntry(id, "attention", "needsSignOut");
    case YdotoolSetupState::DaemonNotRunning:
        return checkEntry(id, "attention", "daemonNotRunning", "start");
    case YdotoolSetupState::ReadyLayoutCaveat:
        return checkEntry(id, "ok", "readyLayoutCaveat");
    case YdotoolSetupState::Ready:
        break;
    }
    return checkEntry(id, "ok", "ready");
}

QString testName(Test test)
{
    switch (test) {
    case Test::Ready:
        return QStringLiteral("ready");
    case Test::Typing:
        return QStringLiteral("typing");
    case Test::Missed:
        return QStringLiteral("missed");
    case Test::Off:
        break;
    }
    return QStringLiteral("off");
}

QString phoneStateName(PhoneTransferState state)
{
    switch (state) {
    case PhoneTransferState::Sent:
        return QStringLiteral("sent");
    case PhoneTransferState::Interrupted:
        return QStringLiteral("interrupted");
    case PhoneTransferState::NoNetwork:
        return QStringLiteral("noNetwork");
    case PhoneTransferState::Failed:
        return QStringLiteral("failed");
    case PhoneTransferState::Waiting:
        break;
    }
    return QStringLiteral("waiting");
}

} // namespace

QuickSystem::QuickSystem(ApplicationController *controller, QObject *parent)
    : QuickSystem(controller, VirtualKeyboardSetup::System{}, SpeecherRemoval::linuxSteps(controller), parent)
{
}

QuickSystem::QuickSystem(ApplicationController *controller, VirtualKeyboardSetup::System virtualKeyboard,
                         SpeecherRemoval::Steps removal, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_virtualKeyboard(controller->settings(), std::move(virtualKeyboard))
    , m_removal(std::move(removal))
{
    g_instance = this;
    connect(controller, &ApplicationController::accessibilityStateChanged, this, &QuickSystem::changed);
    connect(controller, &ApplicationController::globalShortcutChanged, this, &QuickSystem::changed);
    connect(controller, &ApplicationController::globalShortcutSupportChanged, this, &QuickSystem::changed);
    connect(&m_virtualKeyboard, &VirtualKeyboardSetup::changed, this, &QuickSystem::changed);
    connect(&m_virtualKeyboard, &VirtualKeyboardSetup::failed, this,
            [this](const QString &message) { emit announced(message, QStringLiteral("triangle-alert")); });
    connect(&m_removal, &SpeecherRemoval::finished, this, [this](const RemovalReport &report) {
        m_removalReport = {{QStringLiteral("removed"), report.removed},
                           {QStringLiteral("problems"), report.problems},
                           {QStringLiteral("settingsDeleted"), report.settingsDeleted},
                           {QStringLiteral("programFile"), report.programFile}};
        emit removalChanged();
    });
    // What changed while Speecher was in the background, such as a service
    // started by hand or a new sign-in, shows when it comes back.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            m_virtualKeyboard.refresh();
            m_controller->refreshAccessibilityState();
        }
    });
}

QuickSystem::~QuickSystem()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickSystem *QuickSystem::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickSystem::create", "the host makes System before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

bool QuickSystem::accessibility() const
{
    return m_controller->accessibilityEnabled();
}

QVariantList QuickSystem::checks() const
{
    return {microphoneCheck(), accessibilityCheck(m_controller), shortcutCheck(m_controller),
            virtualKeyboardCheck(m_virtualKeyboard)};
}

QStringList QuickSystem::virtualKeyboardActions() const
{
    QStringList actions;
    if (m_virtualKeyboard.operation() != Operation::None) {
        return actions;
    }
    if (m_virtualKeyboard.status().ready()) {
        actions.append(QStringLiteral("disable"));
    }
    if (m_virtualKeyboard.status().speecherManagedSetupInstalled) {
        actions.append(QStringLiteral("remove"));
    }
    return actions;
}

QString QuickSystem::virtualKeyboardTest() const
{
    return testName(m_virtualKeyboard.test());
}

bool QuickSystem::canOpenShortcutSettings() const
{
    return !desktopShortcutSettingsCommand().isEmpty();
}

QString QuickSystem::computerName() const
{
    return QSysInfo::machineHostName();
}

QString QuickSystem::phoneTransfer() const
{
    return m_phone ? phoneStateName(m_phone->state()) : QString();
}

QString QuickSystem::phoneLink() const
{
    return m_phone ? m_phone->link() : QString();
}

QVariantMap QuickSystem::phoneBundle() const
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    return {{QStringLiteral("terms"), settings.vocabulary.size()},
            {QStringLiteral("replacements"), settings.bindings.size()},
            {QStringLiteral("profiles"), settings.refinement.writingProfiles.size()}};
}

bool QuickSystem::removing() const
{
    return m_removal.running();
}

QVariantMap QuickSystem::removal() const
{
    return m_removalReport;
}

QVariantMap QuickSystem::check(const QString &id) const
{
    for (const QVariant &check : checks()) {
        if (check.toMap().value(QStringLiteral("id")).toString() == id) {
            return check.toMap();
        }
    }
    qWarning().noquote() << "System.check: no check" << id;
    return {};
}

void QuickSystem::fix(const QString &id)
{
    const QString action = check(id).value(QStringLiteral("fix")).toString();
    if (action.isEmpty()) {
        return;
    }
    if (id == QLatin1String("desktopAccessibility")) {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            emit announced(error.isEmpty() ? QStringLiteral("Desktop accessibility could not be turned on.") : error,
                           QStringLiteral("triangle-alert"));
        }
        return;
    }
    if (action == QLatin1String("setUp")) {
        m_virtualKeyboard.setUp();
    } else if (action == QLatin1String("start")) {
        m_virtualKeyboard.startService();
    } else if (action == QLatin1String("enable")) {
        m_virtualKeyboard.beginTest();
    }
}

void QuickSystem::disableVirtualKeyboard()
{
    m_virtualKeyboard.disable();
}

void QuickSystem::removeVirtualKeyboard()
{
    m_virtualKeyboard.remove();
}

void QuickSystem::runVirtualKeyboardTest()
{
    m_virtualKeyboard.runTest();
}

void QuickSystem::virtualKeyboardTestReceived(const QString &text)
{
    m_virtualKeyboard.receivedTestText(text);
}

void QuickSystem::cancelVirtualKeyboardTest()
{
    m_virtualKeyboard.cancelTest();
}

void QuickSystem::copy(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void QuickSystem::open(const QString &target)
{
    const QUrl url = QDir::isAbsolutePath(target) ? QUrl::fromLocalFile(target) : QUrl(target, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().isEmpty()) {
        qWarning().noquote() << "System.open: not a link or an absolute path:" << target;
        return;
    }
    if (!QDesktopServices::openUrl(url)) {
        qWarning().noquote() << "System.open: the desktop could not open" << url.toString();
    }
}

void QuickSystem::openShortcutSettings()
{
    QStringList command = desktopShortcutSettingsCommand();
    if (command.isEmpty()) {
        return;
    }
    const QString program = command.takeFirst();
    if (!QProcess::startDetached(program, command)) {
        qWarning().noquote() << "System.openShortcutSettings: could not start" << program;
    }
}

void QuickSystem::startPhoneTransfer()
{
    // A new transfer before the old one goes, so the old code stops working.
    m_phone = std::make_unique<PhoneTransfer>(m_controller->settings()->dictationSnapshot());
    connect(m_phone.get(), &PhoneTransfer::stateChanged, this, &QuickSystem::phoneChanged);
    emit phoneChanged();
}

void QuickSystem::stopPhoneTransfer()
{
    if (!m_phone) {
        return;
    }
    m_phone.reset();
    emit phoneChanged();
}

void QuickSystem::uninstall(bool keepSettings)
{
    if (m_removal.running()) {
        return;
    }
    m_removal.start(!keepSettings);
    emit removalChanged();
}

void QuickSystem::quit()
{
    m_controller->quitApplication();
}

} // namespace speecher
