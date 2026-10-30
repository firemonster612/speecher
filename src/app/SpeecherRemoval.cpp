#include "app/SpeecherRemoval.h"

#include "app/ApplicationController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#ifdef Q_OS_LINUX
#include "output/YdotoolSetup.h"
#include "platform/KeywatchSetup.h"
#endif

#include <QDir>
#include <QThread>

#include <algorithm>
#include <memory>
#include <utility>

namespace speecher {

bool RemovalReport::settingsDeleted() const
{
    return std::ranges::any_of(steps, [](const RemovalStep &step) {
        return step.part == RemovalStep::Part::Settings && step.outcome == RemovalStep::Outcome::Removed;
    });
}

namespace {

using Part = RemovalStep::Part;
using Outcome = RemovalStep::Outcome;

// Runs a step that says why it failed, and records how it went.
void record(QList<RemovalStep> &steps, Part part, const std::function<bool(QString *error)> &run)
{
    QString error;
    const bool removed = run(&error);
    steps.append({part, removed ? Outcome::Removed : Outcome::Failed, removed ? QString() : error});
}

} // namespace

std::optional<SpeecherRemoval::Steps> SpeecherRemoval::onThisComputer(ApplicationController *controller)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(controller);
    return std::nullopt;
#else
    Steps steps;
    steps.removeDesktopIntegration = [] { return removeAppImageIntegration(QDir::homePath()); };
    steps.removeShortcutRegistration = [controller](QString *error) {
        return controller->removeGlobalShortcutRegistration(error);
    };
    // The helpers are separate root installations, shared by every account:
    // leaving one behind would leave a system service and a group membership
    // while the report claimed a clean removal.
    steps.virtualKeyboardInstalled = [] { return YdotoolSetup::probe(false).speecherManagedSetupInstalled; };
    steps.stopVirtualKeyboard = [](QString *error) { return YdotoolSetup::stopUserService(error); };
    steps.removeVirtualKeyboard = [](QString *error) {
        return YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Remove, error);
    };
    steps.keyHelperInstalled = [] { return KeywatchSetup::probe().state != KeywatchSetupState::NotInstalled; };
    steps.removeKeyHelper = [](QString *error) { return KeywatchSetup::remove(error); };
    steps.deleteSecrets = [controller](QString *error) {
        if (controller->secretStore()->deleteKeyringSecrets()) {
            return true;
        }
        *error = controller->secretStore()->lastError();
        return false;
    };
    steps.deleteSettings = [controller](QString *error) { return controller->settings()->deleteAll(error); };
    steps.programFile = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    return steps;
#endif
}

SpeecherRemoval::SpeecherRemoval(Steps steps, QObject *parent)
    : QObject(parent)
    , m_steps(std::move(steps))
{
}

void SpeecherRemoval::start(bool deleteSettings)
{
    if (m_running) {
        return;
    }
    m_running = true;
    RemovalReport report;
    report.programFile = m_steps.programFile;
    report.files = m_steps.removeDesktopIntegration();
    record(report.steps, Part::ShortcutRegistration, m_steps.removeShortcutRegistration);

    const bool virtualKeyboard = m_steps.virtualKeyboardInstalled();
    const bool keyHelper = m_steps.keyHelperInstalled();
    const auto helpers = std::make_shared<RemovalReport>(std::move(report));
    QThread *thread = QThread::create([steps = m_steps, helpers, virtualKeyboard, keyHelper] {
        if (virtualKeyboard) {
            // A service that was not running is no failure.
            QString stopError;
            if (!steps.stopVirtualKeyboard(&stopError) && !stopError.isEmpty()) {
                helpers->steps.append({Part::VirtualKeyboardService, Outcome::Failed, stopError});
            }
            record(helpers->steps, Part::VirtualKeyboard, steps.removeVirtualKeyboard);
        }
        if (keyHelper) {
            record(helpers->steps, Part::KeyHelper, steps.removeKeyHelper);
        }
    });
    connect(thread, &QThread::finished, this,
            [this, helpers, deleteSettings] { finish(std::move(*helpers), deleteSettings); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SpeecherRemoval::finish(RemovalReport report, bool deleteSettings)
{
    if (deleteSettings) {
        record(report.steps, Part::Keyring, m_steps.deleteSecrets);
    }
    // A key whose deletion the keyring refused is hidden by a marker in the
    // settings, so they go only once the keyring's deletions went through.
    if (deleteSettings && report.steps.last().outcome == Outcome::Removed) {
        record(report.steps, Part::Settings, m_steps.deleteSettings);
    } else {
        report.steps.append({Part::Settings, Outcome::Kept, QString()});
    }
    m_running = false;
    emit finished(report);
}

} // namespace speecher
