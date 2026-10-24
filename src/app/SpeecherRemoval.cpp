#include "app/SpeecherRemoval.h"

#include "app/ApplicationController.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "output/YdotoolSetup.h"
#include "platform/KeywatchSetup.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QThread>

#include <memory>
#include <utility>

namespace speecher {

namespace {

// error, or what to say when a step failed without saying why.
QString problem(const QString &error, const QString &fallback)
{
    return error.isEmpty() ? fallback : error;
}

} // namespace

SpeecherRemoval::Steps SpeecherRemoval::linuxSteps(ApplicationController *controller)
{
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
    steps.deleteSettings = [controller](QString *error) {
        QSettings &raw = controller->settings()->raw();
        const QString file = raw.fileName();
        raw.clear();
        raw.sync();
        if (QFile::exists(file) && !QFile::remove(file)) {
            *error = QStringLiteral("Could not delete the settings file at %1.").arg(file);
            return false;
        }
        return true;
    };
    steps.programFile = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    return steps;
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
    const DesktopIntegrationRemoval files = m_steps.removeDesktopIntegration();
    report.removed += files.removed;
    for (const QString &failure : files.failed) {
        report.problems.append(QStringLiteral("Could not remove the %1.").arg(failure));
    }
    QString shortcutError;
    if (m_steps.removeShortcutRegistration(&shortcutError)) {
        report.removed.append(QStringLiteral("Global Shortcut registration"));
    } else {
        report.problems.append(
            problem(shortcutError, QStringLiteral("The Global Shortcut registration could not be removed.")));
    }

    const bool virtualKeyboard = m_steps.virtualKeyboardInstalled();
    const bool keyHelper = m_steps.keyHelperInstalled();
    const auto helpers = std::make_shared<RemovalReport>(std::move(report));
    QThread *thread = QThread::create([steps = m_steps, helpers, virtualKeyboard, keyHelper] {
        if (virtualKeyboard) {
            QString stopError;
            if (!steps.stopVirtualKeyboard(&stopError) && !stopError.isEmpty()) {
                helpers->problems.append(QStringLiteral("Virtual keyboard service: %1").arg(stopError));
            }
            QString error;
            if (steps.removeVirtualKeyboard(&error)) {
                helpers->removed.append(QStringLiteral("virtual keyboard setup"));
            } else {
                helpers->problems.append(problem(error, QStringLiteral("The virtual keyboard setup could not be removed.")));
            }
        }
        if (keyHelper) {
            QString error;
            if (steps.removeKeyHelper(&error)) {
                helpers->removed.append(QStringLiteral("single-key helper"));
            } else {
                helpers->problems.append(problem(error, QStringLiteral("The single-key helper could not be removed.")));
            }
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
        QString error;
        if (m_steps.deleteSecrets(&error)) {
            report.removed.append(QStringLiteral("API keys in the desktop keyring"));
        } else {
            report.problems.append(
                QStringLiteral("Could not delete your API keys from the desktop keyring: %1").arg(error));
        }
        error.clear();
        report.settingsDeleted = m_steps.deleteSettings(&error);
        if (report.settingsDeleted) {
            report.removed.append(QStringLiteral("settings"));
        } else {
            report.problems.append(error);
        }
    }
    m_running = false;
    emit finished(report);
}

} // namespace speecher
