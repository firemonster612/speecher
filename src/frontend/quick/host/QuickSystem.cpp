#include "frontend/quick/host/QuickSystem.h"

#include "app/AccessibilityMonitor.h"
#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickRemoval.h"
#include "output/TextDelivery.h"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QSysInfo>
#include <QUrl>

namespace speecher {

namespace {

// The checks' ids, as checks and fix name them.
const QString microphoneId = QStringLiteral("microphoneAccess");
const QString accessibilityId = QStringLiteral("desktopAccessibility");
const QString shortcutsId = QStringLiteral("globalShortcuts");
const QString keyboardId = QStringLiteral("virtualKeyboardHealth");

QVariantMap checkEntry(const QString &id, const char *state, const char *reason, const char *fix = "")
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("state"), QLatin1String(state)},
            {QStringLiteral("reason"), QLatin1String(reason)},
            {QStringLiteral("fix"), QLatin1String(fix)}};
}

// What the privacy settings say; where Speecher can't change them itself,
// the fix opens them.
QVariantMap microphoneCheck(const PlatformComposition *platform)
{
    switch (platform->microphoneAccess()) {
    case MicrophoneAccess::NotAsked:
        return checkEntry(microphoneId, "ok", "notAsked");
    case MicrophoneAccess::NotDetermined:
        return checkEntry(microphoneId, "ok", "notDetermined");
    case MicrophoneAccess::Denied:
        return checkEntry(microphoneId, "off", "denied", "openSettings");
    case MicrophoneAccess::Restricted:
        return checkEntry(microphoneId, "off", "restricted");
    case MicrophoneAccess::DeviceOff:
        return checkEntry(microphoneId, "off", "deviceOff", "openSettings");
    case MicrophoneAccess::AppsOff:
        return checkEntry(microphoneId, "off", "appsOff", "openSettings");
    case MicrophoneAccess::DesktopAppsOff:
        return checkEntry(microphoneId, "off", "desktopAppsOff", "openSettings");
    case MicrophoneAccess::Allowed:
        break;
    }
    return checkEntry(microphoneId, "ok", "allowed");
}

QVariantMap accessibilityCheck(const ApplicationController *controller)
{
    if (!controller->accessibility()->supported()) {
        return checkEntry(accessibilityId, "off", "unsupported");
    }
    if (!controller->accessibility()->enabled()) {
        return checkEntry(accessibilityId, "off", "off", "enable");
    }
    if (!controller->accessibility()->persistent()) {
        return checkEntry(accessibilityId, "attention", "sessionOnly", "enable");
    }
    return checkEntry(accessibilityId, "ok", "on");
}

QVariantMap shortcutCheck(const ApplicationController *controller)
{
    if (!controller->globalShortcutSupportKnown()) {
        return checkEntry(shortcutsId, "working", "checking");
    }
    if (!controller->globalShortcutsSupported()) {
        return checkEntry(shortcutsId, "off", "unsupported");
    }
    if (controller->globalShortcutDisplay().isEmpty()) {
        return checkEntry(shortcutsId, "attention", "notSet");
    }
    return checkEntry(shortcutsId, "ok", "registered");
}

QVariantMap failureEntry(const QString &step, const QString &error)
{
    return {{QStringLiteral("step"), step}, {QStringLiteral("error"), error}};
}

using Operation = VirtualKeyboardSetup::Operation;
using Test = VirtualKeyboardSetup::Test;
using Failure = VirtualKeyboardSetup::Failure;

QVariantMap virtualKeyboardCheck(const VirtualKeyboardSetup &keyboard)
{
    switch (keyboard.operation()) {
    case Operation::SettingUp:
        return checkEntry(keyboardId, "working", "settingUp");
    case Operation::Starting:
        return checkEntry(keyboardId, "working", "starting");
    case Operation::Removing:
        return checkEntry(keyboardId, "working", "removing");
    case Operation::None:
        break;
    }
    if (keyboard.test() != Test::Off) {
        return checkEntry(keyboardId, "attention", "testing");
    }
    const YdotoolSetupStatus status = keyboard.status();
    switch (status.state) {
    case YdotoolSetupState::Disabled:
        return status.speecherManagedSetupInstalled ? checkEntry(keyboardId, "attention", "disabled", "enable")
                                                    : checkEntry(keyboardId, "attention", "availableOutside", "setUp");
    case YdotoolSetupState::NotInstalled:
        return checkEntry(keyboardId, "attention", "notInstalled", "setUp");
    case YdotoolSetupState::NeedsUinputPermission:
        return checkEntry(keyboardId, "attention", "needsUinputPermission", "setUp");
    case YdotoolSetupState::NeedsSignOut:
        return checkEntry(keyboardId, "attention", "needsSignOut");
    case YdotoolSetupState::DaemonNotRunning:
        return checkEntry(keyboardId, "attention", "daemonNotRunning", "start");
    case YdotoolSetupState::ReadyLayoutCaveat:
        return checkEntry(keyboardId, "ok", "readyLayoutCaveat");
    case YdotoolSetupState::Ready:
        break;
    }
    return checkEntry(keyboardId, "ok", "ready");
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

QString failureName(Failure failure)
{
    switch (failure) {
    case Failure::SetUp:
        return QStringLiteral("setUp");
    case Failure::Start:
        return QStringLiteral("start");
    case Failure::Type:
        return QStringLiteral("type");
    case Failure::Stop:
        return QStringLiteral("stop");
    case Failure::Remove:
        return QStringLiteral("remove");
    case Failure::FilesLeft:
        break;
    }
    return QStringLiteral("filesLeft");
}

// What the default paste needs of the computer with these settings.
TextDelivery::PasteNeed defaultPasteNeed(const OutputSettings &output)
{
    return TextDelivery::pasteNeed(output, globalPasteMethod(output.pasteRules));
}

} // namespace

QuickSystem::QuickSystem(ApplicationController *controller, VirtualKeyboardSetup *virtualKeyboard,
                         const QuickRemoval *removal, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_virtualKeyboard(virtualKeyboard)
    , m_removal(removal)
{
    connect(controller->accessibility(), &AccessibilityMonitor::stateChanged, this, &QuickSystem::changed);
    connect(controller, &ApplicationController::globalShortcutChanged, this, &QuickSystem::changed);
    connect(controller, &ApplicationController::globalShortcutSupportChanged, this, &QuickSystem::changed);
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickSystem::changed);
    connect(m_virtualKeyboard, &VirtualKeyboardSetup::changed, this, &QuickSystem::changed);
    connect(m_virtualKeyboard, &VirtualKeyboardSetup::failed, this, [this](Failure failure, const QString &error) {
        m_failures[keyboardId].append(failureEntry(failureName(failure), error));
        emit changed();
    });
    // What changed while Speecher was in the background, such as a service
    // started by hand, a new sign-in or the microphone allowed in the
    // system's settings, shows when it comes back.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            m_virtualKeyboard->refresh();
            m_controller->accessibility()->refresh();
            emit changed();
        }
    });
}

bool QuickSystem::accessibility() const
{
    return m_controller->accessibility()->enabled();
}

QVariantList QuickSystem::checks() const
{
    QList<QVariantMap> entries{microphoneCheck(m_controller->platform()), accessibilityCheck(m_controller),
                               shortcutCheck(m_controller)};
    if constexpr (OutputMethod::hasVirtualKeyboard) {
        entries.append(virtualKeyboardCheck(*m_virtualKeyboard));
    }
    QVariantList checks;
    for (QVariantMap check : entries) {
        check.insert(QStringLiteral("failures"), m_failures.value(check.value(QStringLiteral("id")).toString()));
        checks.append(check);
    }
    return checks;
}

QStringList QuickSystem::virtualKeyboardActions() const
{
    QStringList actions;
    if (m_virtualKeyboard->operation() != Operation::None) {
        return actions;
    }
    if (m_virtualKeyboard->status().ready()) {
        actions.append(QStringLiteral("disable"));
    }
    if (m_virtualKeyboard->status().speecherManagedSetupInstalled) {
        actions.append(QStringLiteral("remove"));
    }
    return actions;
}

QString QuickSystem::virtualKeyboardTest() const
{
    return testName(m_virtualKeyboard->test());
}

QStringList QuickSystem::pasteRoute() const
{
    const OutputSettings output = m_controller->settings()->dictationSnapshot().output;
    return TextDelivery::pasteRoute(output, globalPasteMethod(output.pasteRules));
}

QString QuickSystem::pasteNeed() const
{
    switch (defaultPasteNeed(m_controller->settings()->dictationSnapshot().output)) {
    case TextDelivery::PasteNeed::Accessibility:
        return QStringLiteral("accessibility");
    case TextDelivery::PasteNeed::VirtualKeyboard:
        return QStringLiteral("keyboard");
    case TextDelivery::PasteNeed::Either:
        return QStringLiteral("either");
    case TextDelivery::PasteNeed::Nothing:
        break;
    }
    return {};
}

bool QuickSystem::wordsStayCopied() const
{
    // Ready as its health row says: set up, with no setup or typing test
    // under way.
    const bool keyboard = virtualKeyboardCheck(*m_virtualKeyboard).value(QStringLiteral("state")) == QLatin1String("ok");
    return TextDelivery::wordsStayCopied(defaultPasteNeed(m_controller->settings()->dictationSnapshot().output),
                                         accessibility(), keyboard);
}

bool QuickSystem::hasVirtualKeyboard() const
{
    return OutputMethod::hasVirtualKeyboard;
}

bool QuickSystem::canOpenShortcutSettings() const
{
    return !m_controller->platform()->shortcutSettingsCommand().isEmpty();
}

bool QuickSystem::canRemove() const
{
    return m_removal->available();
}

bool QuickSystem::canLaunchAtLogin() const
{
    return m_controller->platform()->canLaunchAtLogin();
}

QString QuickSystem::computerName() const
{
    return QSysInfo::machineHostName();
}

QVariantMap QuickSystem::check(const QString &id) const
{
    for (const QVariant &check : checks()) {
        if (check.toMap().value(QStringLiteral("id")).toString() == id) {
            return check.toMap();
        }
    }
    qWarning().noquote() << "System: no check" << id;
    return {};
}

void QuickSystem::fix(const QString &id)
{
    runFix(id, VirtualKeyboardSetup::AfterSetUp::KeepOutputMethod);
}

void QuickSystem::fixFromSetup(const QString &id)
{
    runFix(id, VirtualKeyboardSetup::AfterSetUp::PasteAutomatically);
}

void QuickSystem::runFix(const QString &id, VirtualKeyboardSetup::AfterSetUp afterSetUp)
{
    if (m_removal->running()) {
        return;
    }
    const QString action = check(id).value(QStringLiteral("fix")).toString();
    if (action.isEmpty()) {
        return;
    }
    clearFailures(id);
    if (id == microphoneId) {
        m_controller->platform()->openMicrophoneSettings();
        return;
    }
    if (id == accessibilityId) {
        QString error;
        if (!m_controller->accessibility()->enablePermanently(&error)) {
            m_failures[id] = {failureEntry(QStringLiteral("enable"), error)};
            emit changed();
        }
        return;
    }
    if (action == QLatin1String("setUp")) {
        m_virtualKeyboard->setUp(afterSetUp);
    } else if (action == QLatin1String("start")) {
        m_virtualKeyboard->startService();
    } else if (action == QLatin1String("enable")) {
        m_virtualKeyboard->beginTest();
    }
}

void QuickSystem::clearFailures(const QString &id)
{
    if (m_failures.remove(id) > 0) {
        emit changed();
    }
}

void QuickSystem::disableVirtualKeyboard()
{
    if (m_removal->running()) {
        return;
    }
    clearFailures(keyboardId);
    m_virtualKeyboard->disable();
}

void QuickSystem::removeVirtualKeyboard()
{
    if (m_removal->running()) {
        return;
    }
    clearFailures(keyboardId);
    m_virtualKeyboard->remove();
}

void QuickSystem::runVirtualKeyboardTest()
{
    if (m_removal->running()) {
        return;
    }
    clearFailures(keyboardId);
    m_virtualKeyboard->runTest();
}

void QuickSystem::virtualKeyboardTestReceived(const QString &text)
{
    m_virtualKeyboard->receivedTestText(text);
}

void QuickSystem::cancelVirtualKeyboardTest()
{
    m_virtualKeyboard->cancelTest();
}

void QuickSystem::copy(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void QuickSystem::open(const QString &target)
{
    // Release notes come from the network, so a link in them must not reach
    // a file share or another app's scheme.
    const bool path = QDir::isAbsolutePath(target);
    const QUrl url = path ? QUrl::fromLocalFile(target) : QUrl(target, QUrl::StrictMode);
    const bool webLink = url.isValid() && (url.scheme() == u"https" || url.scheme() == u"http") && !url.host().isEmpty();
    if (!path && !webLink) {
        qWarning().noquote() << "System.open: not a web link or an absolute path:" << target;
        return;
    }
    if (!QDesktopServices::openUrl(url)) {
        qWarning().noquote() << "System.open: the desktop could not open" << url.toString();
    }
}

void QuickSystem::openShortcutSettings()
{
    QStringList command = m_controller->platform()->shortcutSettingsCommand();
    if (command.isEmpty()) {
        return;
    }
    const QString program = command.takeFirst();
    if (!QProcess::startDetached(program, command)) {
        qWarning().noquote() << "System.openShortcutSettings: could not start" << program;
    }
}

void QuickSystem::quit()
{
    m_controller->quitApplication();
}

} // namespace speecher
