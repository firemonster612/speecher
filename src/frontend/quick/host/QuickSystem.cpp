#include "frontend/quick/host/QuickSystem.h"

#include "app/ApplicationController.h"
#include "app/PhoneTransfer.h"
#include "core/SettingsStore.h"
#include "output/TextDelivery.h"
#ifdef Q_OS_LINUX
#include "platform/LinuxDesktopIntegration.h"
#endif

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

const QString accessibilityId = QStringLiteral("desktopAccessibility");

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

QVariantMap failureEntry(const QString &step, const QString &error)
{
    return {{QStringLiteral("step"), step}, {QStringLiteral("error"), error}};
}

#ifdef Q_OS_LINUX
using Operation = VirtualKeyboardSetup::Operation;
using Test = VirtualKeyboardSetup::Test;
using Failure = VirtualKeyboardSetup::Failure;

const QString keyboardId = QStringLiteral("virtualKeyboardHealth");

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

QString partName(RemovalStep::Part part)
{
    switch (part) {
    case RemovalStep::Part::ShortcutRegistration:
        return QStringLiteral("shortcutRegistration");
    case RemovalStep::Part::VirtualKeyboardService:
        return QStringLiteral("virtualKeyboardService");
    case RemovalStep::Part::VirtualKeyboard:
        return QStringLiteral("virtualKeyboard");
    case RemovalStep::Part::KeyHelper:
        return QStringLiteral("keyHelper");
    case RemovalStep::Part::Keyring:
        return QStringLiteral("keyring");
    case RemovalStep::Part::Settings:
        break;
    }
    return QStringLiteral("settings");
}

QString outcomeName(RemovalStep::Outcome outcome)
{
    switch (outcome) {
    case RemovalStep::Outcome::Removed:
        return QStringLiteral("removed");
    case RemovalStep::Outcome::Failed:
        return QStringLiteral("failed");
    case RemovalStep::Outcome::Kept:
        break;
    }
    return QStringLiteral("kept");
}

QVariantMap reportMap(const RemovalReport &report)
{
    QVariantList steps;
    for (const RemovalStep &step : report.steps) {
        steps.append(QVariantMap{{QStringLiteral("part"), partName(step.part)},
                                 {QStringLiteral("outcome"), outcomeName(step.outcome)},
                                 {QStringLiteral("error"), step.error}});
    }
    return {{QStringLiteral("files"), QVariantMap{{QStringLiteral("removed"), report.files.removed},
                                                  {QStringLiteral("absent"), report.files.absent},
                                                  {QStringLiteral("failed"), report.files.failed}}},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("settingsDeleted"), report.settingsDeleted()},
            {QStringLiteral("programFile"), report.programFile}};
}
#endif

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

#ifdef Q_OS_LINUX
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
#else
QuickSystem::QuickSystem(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
#endif
{
    g_instance = this;
    connect(controller, &ApplicationController::accessibilityStateChanged, this, &QuickSystem::changed);
    connect(controller, &ApplicationController::globalShortcutChanged, this, &QuickSystem::changed);
    connect(controller, &ApplicationController::globalShortcutSupportChanged, this, &QuickSystem::changed);
    connect(controller->settings(), &SettingsStore::snapshotApplied, this, &QuickSystem::changed);
#ifdef Q_OS_LINUX
    connect(&m_virtualKeyboard, &VirtualKeyboardSetup::changed, this, &QuickSystem::changed);
    connect(&m_virtualKeyboard, &VirtualKeyboardSetup::changed, this, &QuickSystem::startAskedRemoval);
    connect(&m_virtualKeyboard, &VirtualKeyboardSetup::failed, this, [this](Failure failure, const QString &error) {
        m_failures[keyboardId].append(failureEntry(failureName(failure), error));
        emit changed();
    });
    connect(&m_removal, &SpeecherRemoval::finished, this, [this](const RemovalReport &report) {
        m_removalReport = reportMap(report);
        emit removalChanged();
    });
#endif
    // What changed while Speecher was in the background, such as a service
    // started by hand or a new sign-in, shows when it comes back.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
#ifdef Q_OS_LINUX
            m_virtualKeyboard.refresh();
#endif
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
    QList<QVariantMap> entries{microphoneCheck(), accessibilityCheck(m_controller), shortcutCheck(m_controller)};
#ifdef Q_OS_LINUX
    entries.append(virtualKeyboardCheck(m_virtualKeyboard));
#endif
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
#ifdef Q_OS_LINUX
    if (m_virtualKeyboard.operation() != Operation::None) {
        return actions;
    }
    if (m_virtualKeyboard.status().ready()) {
        actions.append(QStringLiteral("disable"));
    }
    if (m_virtualKeyboard.status().speecherManagedSetupInstalled) {
        actions.append(QStringLiteral("remove"));
    }
#endif
    return actions;
}

QString QuickSystem::virtualKeyboardTest() const
{
#ifdef Q_OS_LINUX
    return testName(m_virtualKeyboard.test());
#else
    return QStringLiteral("off");
#endif
}

QStringList QuickSystem::pasteRoute() const
{
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    // With no default paste of its own, Speecher copies, as delivery does.
    const PasteRule *rule = findPasteRule(settings.output.pasteRules, PasteRuleScope::Global, QString());
    return TextDelivery::pasteRoute(settings.output,
                                    rule && rule->enabled ? rule->method : PasteMethod::ClipboardOnly);
}

bool QuickSystem::canOpenShortcutSettings() const
{
#ifdef Q_OS_LINUX
    return !desktopShortcutSettingsCommand().isEmpty();
#else
    return false;
#endif
}

bool QuickSystem::canRemove() const
{
    // Elsewhere Speecher sets up nothing outside its own app, which goes the
    // platform's way: the Trash, or Windows' uninstaller.
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
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
    return m_phoneBundle;
}

bool QuickSystem::removing() const
{
#ifdef Q_OS_LINUX
    return m_removalAsked || m_removal.running();
#else
    return false;
#endif
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
    if (removing()) {
        return;
    }
    const QString action = check(id).value(QStringLiteral("fix")).toString();
    if (action.isEmpty()) {
        return;
    }
    clearFailures(id);
    if (id == accessibilityId) {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_failures[id] = {failureEntry(QStringLiteral("enable"), error)};
            emit changed();
        }
        return;
    }
#ifdef Q_OS_LINUX
    if (action == QLatin1String("setUp")) {
        m_virtualKeyboard.setUp(afterSetUp);
    } else if (action == QLatin1String("start")) {
        m_virtualKeyboard.startService();
    } else if (action == QLatin1String("enable")) {
        m_virtualKeyboard.beginTest();
    }
#else
    Q_UNUSED(afterSetUp);
#endif
}

void QuickSystem::clearFailures(const QString &id)
{
    if (m_failures.remove(id) > 0) {
        emit changed();
    }
}

void QuickSystem::disableVirtualKeyboard()
{
#ifdef Q_OS_LINUX
    clearFailures(keyboardId);
    m_virtualKeyboard.disable();
#endif
}

void QuickSystem::removeVirtualKeyboard()
{
#ifdef Q_OS_LINUX
    if (removing()) {
        return;
    }
    clearFailures(keyboardId);
    m_virtualKeyboard.remove();
#endif
}

void QuickSystem::runVirtualKeyboardTest()
{
#ifdef Q_OS_LINUX
    if (removing()) {
        return;
    }
    clearFailures(keyboardId);
    m_virtualKeyboard.runTest();
#endif
}

void QuickSystem::virtualKeyboardTestReceived(const QString &text)
{
#ifdef Q_OS_LINUX
    m_virtualKeyboard.receivedTestText(text);
#else
    Q_UNUSED(text);
#endif
}

void QuickSystem::cancelVirtualKeyboardTest()
{
#ifdef Q_OS_LINUX
    m_virtualKeyboard.cancelTest();
#endif
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
#ifdef Q_OS_LINUX
    QStringList command = desktopShortcutSettingsCommand();
    if (command.isEmpty()) {
        return;
    }
    const QString program = command.takeFirst();
    if (!QProcess::startDetached(program, command)) {
        qWarning().noquote() << "System.openShortcutSettings: could not start" << program;
    }
#endif
}

void QuickSystem::startPhoneTransfer()
{
    // A new transfer before the old one goes, so the old code stops working.
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    m_phone = std::make_unique<PhoneTransfer>(settings);
    m_phoneBundle = {{QStringLiteral("terms"), settings.vocabulary.size()},
                     {QStringLiteral("replacements"), settings.bindings.size()},
                     {QStringLiteral("profiles"), settings.refinement.writingProfiles.size()}};
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
#ifdef Q_OS_LINUX
    if (removing()) {
        return;
    }
    m_removalAsked = keepSettings ? RemovalAsked::KeepSettings : RemovalAsked::DeleteSettings;
    startAskedRemoval();
    emit removalChanged();
#else
    Q_UNUSED(keepSettings);
    qWarning().noquote() << "System.uninstall: Speecher removes itself only on Linux";
#endif
}

#ifdef Q_OS_LINUX
void QuickSystem::startAskedRemoval()
{
    // The keyboard's setup saves settings and runs the helper removal runs
    // too, so removal waits for an operation under way and ends the test.
    if (!m_removalAsked || m_virtualKeyboard.operation() != Operation::None) {
        return;
    }
    const bool deleteSettings = *std::exchange(m_removalAsked, std::nullopt) == RemovalAsked::DeleteSettings;
    m_virtualKeyboard.cancelTest();
    m_removal.start(deleteSettings);
}
#endif

void QuickSystem::quit()
{
    m_controller->quitApplication();
}

} // namespace speecher
