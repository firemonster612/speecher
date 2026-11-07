#include "frontend/quick/host/QuickSystem.h"

#include "app/AccessibilityMonitor.h"
#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/DeliverySteps.h"
#include "core/PasteKeys.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickRemoval.h"
#ifdef SPEECHER_WITH_WAYLAND
#include "output/WlClipboardDelivery.h"
#endif

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
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

// Whether Keep can leave words on the clipboard: a Wayland session needs
// Speecher's clipboard helper or wl-copy, everywhere else Qt's clipboard does.
bool clipboardReady()
{
#ifdef SPEECHER_WITH_WAYLAND
    return !WlClipboardDelivery::isWaylandSession() || WlClipboardDelivery::isAvailable();
#else
    return true;
#endif
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
    if constexpr (PasteKeys::hasVirtualKeyboard) {
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
    return DeliveryPlan{m_controller->settings()->dictationSnapshot().output.steps}.route();
}

QVariantList QuickSystem::deliverySteps() const
{
    const DeliverySteps steps = m_controller->settings()->dictationSnapshot().output.steps;
    // Accessibility on only for the session counts; on Windows it is always on.
    const bool accessibility = this->accessibility();
#if defined(Q_OS_MACOS)
    // ⌘V reaches another app only through the Accessibility grant.
    const bool pressReady = accessibility;
#elif defined(Q_OS_WIN)
    const bool pressReady = true;
#else
    // Ready as its health row says: set up, with no setup or typing test
    // under way.
    const bool pressReady =
        virtualKeyboardCheck(*m_virtualKeyboard).value(QStringLiteral("state")) == QLatin1String("ok");
#endif
    const auto entry = [&steps](DeliveryStep step, bool ready) {
        return QVariantMap{{QStringLiteral("id"), deliveryStepName(step)},
                           {QStringLiteral("on"), steps.has(step)},
                           {QStringLiteral("ready"), ready}};
    };
    return {entry(DeliveryStep::Insert, accessibility), entry(DeliveryStep::Press, pressReady),
            entry(DeliveryStep::Keep, clipboardReady())};
}

bool QuickSystem::wordsStayCopied() const
{
    bool reachesApps = false;
    bool getsPastClipboard = false;
    for (const QVariant &entry : deliverySteps()) {
        const QVariantMap step = entry.toMap();
        if (step.value(QStringLiteral("id")) == QLatin1String("keep") || !step.value(QStringLiteral("on")).toBool()) {
            continue;
        }
        reachesApps = true;
        getsPastClipboard = getsPastClipboard || step.value(QStringLiteral("ready")).toBool();
    }
    return reachesApps && !getsPastClipboard;
}

QString QuickSystem::stepRuleNeeds(const QString &method) const
{
    const auto step = speecher::stepRuleNeeds(m_controller->settings()->dictationSnapshot().output.steps,
                                              pasteMethodFromName(method));
    return step ? deliveryStepName(*step) : QString();
}

bool QuickSystem::hasVirtualKeyboard() const
{
    return PasteKeys::hasVirtualKeyboard;
}

bool QuickSystem::canOpenShortcutSettings() const
{
    return !m_controller->platform()->shortcutSettingsCommand().isEmpty();
}

bool QuickSystem::canRemove() const
{
    return m_removal->available();
}

bool QuickSystem::canDisablePermanentAccessibility() const
{
    return m_controller->platform()->canDisablePermanentAccessibility();
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

void QuickSystem::fixFromSetup(const QString &id)
{
    fix(id);
}

void QuickSystem::fix(const QString &id)
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
        m_virtualKeyboard->setUp();
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

void QuickSystem::disablePermanentAccessibility()
{
    if (m_removal->running()) {
        return;
    }
    clearFailures(accessibilityId);
    QString error;
    if (!m_controller->accessibility()->disablePermanently(&error)) {
        m_failures[accessibilityId] = {failureEntry(QStringLiteral("disable"), error)};
    }
    // The files may be gone while the state the monitor reports stays.
    emit changed();
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
    // a file, a file share (//server/share) or another app's scheme.
    const QUrl url(target, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http") || url.host().isEmpty()) {
        qWarning().noquote() << "System.open: not a web link:" << target;
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
