#include "app/SpeecherRemoval.h"

#include "app/ApplicationController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#ifdef Q_OS_LINUX
#include "output/YdotoolSetup.h"
#include "platform/KeywatchSetup.h"
#include "platform/atspi/AtSpiAccess.h"
#endif

#include <QDir>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <memory>
#include <utility>

namespace speecher {

bool RemovalReport::settingsDeleted() const
{
    return std::ranges::any_of(steps, [](const RemovalStep &step) {
        return step.part == RemovalStep::Part::Settings
            && (step.outcome == RemovalStep::Outcome::Removed || step.outcome == RemovalStep::Outcome::KeptHidingKeys);
    });
}

namespace {

using Part = RemovalStep::Part;
using Outcome = RemovalStep::Outcome;

// Runs a step that says why it failed and records how it went, done when it
// went through.
void record(QList<RemovalStep> &steps, Part part, const std::function<bool(QString *error)> &run,
            Outcome done = Outcome::Removed)
{
    QString error;
    const bool ran = run(&error);
    steps.append({part, ran ? done : Outcome::Failed, ran ? QString() : error});
}

} // namespace

std::optional<SpeecherRemoval::Steps> SpeecherRemoval::onThisComputer(ApplicationController *controller)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(controller);
    return std::nullopt;
#else
    Steps steps;
    steps.removeDesktopIntegration = [] {
        DesktopIntegrationRemoval removal = removeAppImageIntegration(QDir::homePath());
        atspi::removePermanentAccessibility(removal);
        return removal;
    };
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
    steps.deleteSecrets = [controller](Steps::SecretsDeleted done) {
        controller->secretStore()->deleteKeyringSecrets(std::move(done));
    };
    steps.settingsHideKeys = [controller] { return !controller->secretStore()->deletionMarkers().isEmpty(); };
    steps.deleteSettings = [controller](QString *error) { return controller->settings()->deleteAll(error); };
    steps.deleteSettingsButMarkers = [controller](QString *error) {
        return controller->settings()->deleteAllButDeletionMarkers(error);
    };
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
    if (!deleteSettings) {
        report.steps.append({Part::Settings, Outcome::Kept, QString()});
        m_running = false;
        emit finished(report);
        return;
    }
    m_steps.deleteSecrets([self = QPointer(this), report](bool deleted, const QString &error) mutable {
        if (!self) {
            return;
        }
        report.steps.append({Part::Keyring, deleted ? Outcome::Removed : Outcome::Failed, deleted ? QString() : error});
        // A key whose deletion the keyring refused stays hidden only while
        // its deletion marker does.
        if (self->m_steps.settingsHideKeys()) {
            record(report.steps, Part::Settings, self->m_steps.deleteSettingsButMarkers, Outcome::KeptHidingKeys);
        } else {
            record(report.steps, Part::Settings, self->m_steps.deleteSettings);
        }
        self->m_running = false;
        emit self->finished(report);
    });
}

} // namespace speecher
