#include "ui/settings/SettingsPageSet.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/AppSettings.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "frontend/qt/SchemaSettingsPage.h"
#ifdef Q_OS_LINUX
#include "platform/LinuxDesktopIntegration.h"
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif
#include "ui/Theme.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>

namespace speecher {

namespace {

SettingsPage providerRowsPage(const SettingsPage &source,
                              const QStringList &rowIds,
                              bool includeSectionHelp)
{
    SettingsPage page = source;
    page.sections.clear();
    for (const SettingsSection &sourceSection : source.sections) {
        SettingsSection section{sourceSection.title,
                                includeSectionHelp ? sourceSection.help : QString(),
                                {}};
        for (const SettingsRow &row : sourceSection.rows) {
            if (rowIds.contains(row.id)) {
                section.rows.append(row);
            }
        }
        if (!section.rows.isEmpty()) {
            page.sections.append(std::move(section));
        }
    }
    return page;
}

SettingsRow *rowById(SettingsPage &page, const QString &id)
{
    for (SettingsSection &section : page.sections) {
        for (SettingsRow &row : section.rows) {
            if (row.id == id) {
                return &row;
            }
        }
    }
    qWarning().noquote() << "settings schema cannot find row" << id;
    return nullptr;
}

SettingsPage &pageById(SettingsSchema &schema, const QString &id)
{
    for (SettingsPage &page : schema.pages) {
        if (page.id == id) {
            return page;
        }
    }
    qFatal("settings schema cannot find page %s", qPrintable(id));
}

SettingsSchema settingsSchema(ApplicationController *controller)
{
    SettingsSchema schema = buildSettingsSchema(qtSchemaContext(
        *controller->platform(),
        *controller->providerRegistry(),
        controller->pendingWhatsNewVersion()));
    UpdateController *updates = controller->updates();
    SettingsPage &general = pageById(schema, QStringLiteral("general"));

    if (SettingsRow *check = rowById(general, QStringLiteral("checkForUpdates"))) {
        check->helpValue = [updates](const AppSettings &settings) {
            const QString channel = settings.updates.channel == UpdateChannel::Nightly
                ? QStringLiteral("Nightly Build")
                : QStringLiteral("Stable Release");
            switch (updates->state()) {
            case UpdateController::State::Idle:
                return QStringLiteral("Check the %1 feed for a newer build.").arg(channel);
            case UpdateController::State::Checking:
                return QStringLiteral("Checking the %1 feed.").arg(channel);
            case UpdateController::State::CheckFailed:
                return updates->errorMessage();
            case UpdateController::State::UpToDate:
                return QStringLiteral("Speecher is up to date.");
            case UpdateController::State::UpdateAvailable:
                return QStringLiteral("Speecher %1 is available.").arg(updates->availableVersion());
            case UpdateController::State::Downloading:
                return QStringLiteral("Downloading Speecher %1 (%2%)")
                    .arg(updates->availableVersion())
                    .arg(updates->downloadPercent());
            case UpdateController::State::ReadyToRestart:
                return updates->errorMessage().isEmpty()
                    ? QStringLiteral("Restart to finish updating.")
                    : updates->errorMessage();
            case UpdateController::State::RestartPending:
                return QStringLiteral("Restarting after this dictation…");
            case UpdateController::State::Restarting:
                return QStringLiteral("Restarting…");
            case UpdateController::State::Error:
                return updates->errorMessage();
            }
            return QString();
        };
        check->value = [updates](const AppSettings &) {
            switch (updates->state()) {
            case UpdateController::State::Checking:
                return QVariant(QStringLiteral("Checking…"));
            case UpdateController::State::UpToDate:
                return QVariant(QStringLiteral("Check again"));
            case UpdateController::State::UpdateAvailable:
                return QVariant(QStringLiteral("Update now"));
            case UpdateController::State::Downloading:
                return QVariant(QStringLiteral("Downloading…"));
            case UpdateController::State::RestartPending:
                return QVariant(QStringLiteral("Restarting after this dictation…"));
            case UpdateController::State::Restarting:
                return QVariant(QStringLiteral("Restarting…"));
            case UpdateController::State::CheckFailed:
            case UpdateController::State::Error:
                return QVariant(QStringLiteral("Try again"));
            default:
                return QVariant(QStringLiteral("Check now"));
            }
        };
        check->enabled = [updates](const AppSettings &, const Capabilities &) {
            return updates->state() != UpdateController::State::Checking
                && updates->state() != UpdateController::State::Downloading
                && updates->state() != UpdateController::State::ReadyToRestart
                && updates->state() != UpdateController::State::RestartPending
                && updates->state() != UpdateController::State::Restarting;
        };
    }

    if (SettingsRow *version = rowById(general, QStringLiteral("currentVersion"))) {
        version->value = [updates](const AppSettings &) {
            QString text = updates->currentVersion();
            if (!updates->availableVersion().isEmpty()) {
                text += QStringLiteral(" — %1 available").arg(updates->availableVersion());
            }
            return QVariant(text);
        };
    }

    return schema;
}

SchemaCustomRow whatsNewCustomRow(const SettingsRow &descriptor,
                                  QWidget *parent,
                                  std::function<void()>)
{
    if (descriptor.id != QStringLiteral("whatsNewNotes")) {
        return {};
    }
    auto *notes = new QLabel(parent);
    notes->setTextFormat(Qt::MarkdownText);
    notes->setTextInteractionFlags(Qt::TextBrowserInteraction);
    notes->setOpenExternalLinks(false);
    QObject::connect(notes, &QLabel::linkActivated, notes, [](const QString &link) {
        const QUrl url(link);
        if (url.scheme() == QStringLiteral("https")) {
            QDesktopServices::openUrl(url);
        }
    });
    notes->setWordWrap(true);
    return {notes,
            {},
            [notes](const QVariant &value) { notes->setText(value.toString()); },
            true};
}

SchemaCustomRowFactory generalCustomRows(ApplicationController *controller)
{
#ifdef Q_OS_LINUX
    return [controller](const SettingsRow &descriptor,
                        QWidget *parent,
                        std::function<void()>) {
        if (descriptor.id != QStringLiteral("globalShortcut")) {
            return SchemaCustomRow{};
        }
        auto *page = new LinuxGlobalShortcutSetupPage(*controller, parent);
        page->hideAppMenuIntegration();
        return SchemaCustomRow{page, {}, {}, true};
    };
#else
    Q_UNUSED(controller)
    return {};
#endif
}

} // namespace

SettingsPageSet::SettingsPageSet(ApplicationController *controller, QWidget *parent)
    : SettingsPageSet(controller, parent, settingsSchema(controller))
{
}

SettingsPageSet::SettingsPageSet(ApplicationController *controller,
                                 QWidget *parent,
                                 SettingsSchema schema)
    : QObject(parent)
    , m_controller(controller)
    , m_schema(std::move(schema))
    , m_outputRows(*controller->settings())
    , m_providerRows(*controller->settings(), *controller->secretStore())
    , m_general(addPage(QStringLiteral("general"), parent, generalCustomRows(controller)))
    , m_audio(addPage(QStringLiteral("audio"), parent))
    , m_applications(addPage(QStringLiteral("applications"), parent))
    , m_output(addPage(QStringLiteral("output"), parent, m_outputRows.factory()))
    , m_refinement(addPage(QStringLiteral("refinement"), parent))
    , m_vocabulary(addPage(QStringLiteral("vocabulary"), parent))
    , m_corrections(addPage(QStringLiteral("corrections"), parent))
    , m_bindings(addPage(QStringLiteral("bindings"), parent, m_bindingRows.factory()))
    , m_providerModels(addPage(
          providerRowsPage(m_schema.page(QStringLiteral("providers")),
                           providerModelRowIds(),
                           false),
          parent))
    , m_providerAuth(addPage(
          providerRowsPage(m_schema.page(QStringLiteral("providers")),
                           providerAuthRowIds(),
                           true),
          parent,
          m_providerRows.factory()))
    , m_whatsNew(addPage(QStringLiteral("whatsNew"), parent, whatsNewCustomRow))
{
    connect(controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &SettingsPageSet::updateAccessibilityState);
    connect(controller->updates(),
            &UpdateController::changed,
            this,
            &SettingsPageSet::refreshUpdateRows);
    connect(this, &SettingsPageSet::changed,
            this, &SettingsPageSet::refreshUpdateRows);
    updateAccessibilityState(controller->accessibilitySupported(),
                             controller->accessibilityEnabled(),
                             controller->accessibilityPersistent());
    refreshUpdateRows();
}

// Every row of the schema's providers page must appear in exactly one of these
// lists, or it silently never renders on the Qt frontend; the schema tests
// check that coverage.
QStringList SettingsPageSet::providerModelRowIds()
{
    return {QStringLiteral("openAiModel"),
            QStringLiteral("openAiModelCaution"),
            QStringLiteral("openAiEffort"),
            QStringLiteral("openAiFastMode"),
            QStringLiteral("anthropicModel"),
            QStringLiteral("anthropicModelCaution"),
            QStringLiteral("anthropicEffort"),
            QStringLiteral("anthropicFastMode")};
}

QStringList SettingsPageSet::providerAuthRowIds()
{
    return {QStringLiteral("openAiAuthMode"),
            QStringLiteral("openAiCliproxyAccount"),
            QStringLiteral("openAiAuth"),
            QStringLiteral("anthropicAuthMode"),
            QStringLiteral("anthropicCliproxyAccount"),
            QStringLiteral("cliproxyBaseUrl"),
            QStringLiteral("cliproxyApiKey")};
}

SchemaSettingsPage *SettingsPageSet::addPage(const QString &id,
                                             QWidget *parent,
                                             SchemaCustomRowFactory customRows)
{
    return addPage(m_schema.page(id), parent, std::move(customRows));
}

SchemaSettingsPage *SettingsPageSet::addPage(const SettingsPage &descriptor,
                                             QWidget *parent,
                                             SchemaCustomRowFactory customRows)
{
    auto *page = new SchemaSettingsPage(descriptor, parent, std::move(customRows));
    connect(page, &SchemaSettingsPage::changed, this, [this, page] {
        page->appendToDraft(m_draft);
        for (SchemaSettingsPage *candidate : std::as_const(m_pages)) {
            const QSignalBlocker blocker(candidate);
            candidate->load(m_draft);
        }
        emit changed();
    });
    connect(page, &SchemaSettingsPage::actionTriggered, this, &SettingsPageSet::runPageAction);
    m_pages.append(page);
    return page;
}

SchemaSettingsPage *SettingsPageSet::general() const { return m_general; }
SchemaSettingsPage *SettingsPageSet::audio() const { return m_audio; }
SchemaSettingsPage *SettingsPageSet::applications() const { return m_applications; }
SchemaSettingsPage *SettingsPageSet::output() const { return m_output; }
SchemaSettingsPage *SettingsPageSet::refinement() const { return m_refinement; }
SchemaSettingsPage *SettingsPageSet::providerModels() const { return m_providerModels; }
SchemaSettingsPage *SettingsPageSet::providerAuth() const { return m_providerAuth; }
SchemaSettingsPage *SettingsPageSet::vocabulary() const { return m_vocabulary; }
SchemaSettingsPage *SettingsPageSet::corrections() const { return m_corrections; }
SchemaSettingsPage *SettingsPageSet::bindings() const { return m_bindings; }
SchemaSettingsPage *SettingsPageSet::whatsNew() const { return m_whatsNew; }

void SettingsPageSet::load()
{
    loadBeforeShow();
    loadAfterShow();
}

void SettingsPageSet::loadBeforeShow()
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
    m_draft = snapshot;
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        const QSignalBlocker blocker(page);
        page->load(snapshot);
    }
    m_outputRows.refresh();
    refreshUpdateRows();
}

void SettingsPageSet::loadAfterShow()
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
    m_draft = snapshot;
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        const QSignalBlocker blocker(page);
        page->loadExpensiveRows(snapshot);
    }
    m_providerRows.loadSecret();
    refreshUpdateRows();
}

bool SettingsPageSet::save(bool showValidationErrors,
                           bool refreshPages,
                           SaveOutcome *outcome)
{
    if (outcome) *outcome = {};
    if (m_settingsDeletionStarted) {
        return false;
    }
    const auto refuse = [outcome](SaveFailure failure, const QStringList &messages) {
        if (outcome) *outcome = {failure, messages};
        return false;
    };
    const auto refuseAloud = [&](SaveFailure failure,
                                 QWidget *page,
                                 const QString &title,
                                 const QStringList &messages) {
        if (showValidationErrors) {
            QMessageBox::warning(page, title, messages.join(QLatin1Char('\n')));
        }
        return refuse(failure, messages);
    };

    SettingsStore *settings = m_controller->settings();
    const QStringList replacementProblems = m_bindings->validate();
    if (!replacementProblems.isEmpty()) {
        return refuseAloud(SaveFailure::InvalidReplacementRules,
                           m_bindings,
                           QStringLiteral("Replacements not saved"),
                           replacementProblems);
    }
    const QStringList pasteRuleProblems = m_output->validate();
    if (!pasteRuleProblems.isEmpty()) {
        return refuseAloud(SaveFailure::DuplicatePasteRuleIds,
                           m_output,
                           QStringLiteral("Paste rules not saved"),
                           pasteRuleProblems);
    }

    settings->applySnapshot(m_draft);
    Theme::apply(settings->theme());
    // Applying the theme is the moment that reveals whether the platform
    // honours it, which the Theme row's gate depends on.
    applyCapabilities();
    const AppSettings snapshot = settings->snapshot();
    m_draft = snapshot;
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        const QSignalBlocker blocker(page);
        page->load(snapshot);
    }
    // saveSecret says why itself, because only it knows what the keyring said.
    if (!m_providerRows.saveSecret()) {
        return refuse(SaveFailure::ProviderSecret,
                      {QStringLiteral("Settings saved, but provider credentials could not be saved")});
    }
    if (refreshPages) {
        load();
    } else {
        m_outputRows.refresh();
    }
    return true;
}

void SettingsPageSet::prepareForSettingsDeletion()
{
    if (m_settingsDeletionStarted) {
        return;
    }
    m_settingsDeletionStarted = true;
    emit settingsDeletionStarted();
}

void SettingsPageSet::preserveBindingScroll(QScrollArea *scroll)
{
    connect(&m_bindingRows, &BindingRows::preserveScrollRequested,
            scroll, [scroll](bool rebuilding) {
                QScrollBar *bar = scroll->verticalScrollBar();
                if (rebuilding) {
                    scroll->setProperty("preservedScroll", bar->value());
                    return;
                }
                const auto restore = [scroll] {
                    QScrollBar *currentBar = scroll->verticalScrollBar();
                    currentBar->setValue(qMin(scroll->property("preservedScroll").toInt(),
                                              currentBar->maximum()));
                };
                restore();
                QTimer::singleShot(0, scroll, restore);
            });
}

void SettingsPageSet::runPageAction(const QString &rowId)
{
    if (rowId == QStringLiteral("runSetup")) {
        m_controller->showSetupAssistant();
        return;
    }
    if (rowId == QStringLiteral("checkForUpdates")) {
        if (m_controller->updates()->state() == UpdateController::State::UpdateAvailable) {
            m_controller->updates()->updateNow();
        } else {
            m_controller->updates()->checkForUpdates(m_draft.updates.channel);
        }
        return;
    }
    if (rowId == QStringLiteral("whatsNew")) {
        m_controller->clearPendingWhatsNew();
        emit whatsNewRequested();
        return;
    }
#ifdef Q_OS_LINUX
    if (rowId == QStringLiteral("removeSpeecher")) {
        removeSpeecher();
        return;
    }
#endif
    if (rowId == QStringLiteral("enableAccessibility")) {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            QMessageBox::warning(qobject_cast<QWidget *>(parent()),
                                 QStringLiteral("Desktop accessibility"),
                                 error.isEmpty()
                                     ? QStringLiteral("Desktop accessibility could not be turned on.")
                                     : error);
        }
    }
}

void SettingsPageSet::refreshUpdateRows()
{
    m_general->refresh();
    m_whatsNew->refresh();
}

#ifdef Q_OS_LINUX
// One confirmation, then everything Speecher set up for this user is undone
// and the outcome is reported item by item. The program file itself stays: a
// running AppImage cannot delete itself safely, and the person knows where
// they put it.
void SettingsPageSet::removeSpeecher()
{
    QWidget *window = qobject_cast<QWidget *>(parent());
    QMessageBox confirm(window);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(QStringLiteral("Remove Speecher"));
    confirm.setText(QStringLiteral("Remove Speecher from this computer?"));
    confirm.setInformativeText(QStringLiteral(
        "This removes the app menu entry, the speecher command, the app icon and the Global "
        "Shortcut registration. The Speecher program file stays where you put it."));
    auto *deleteSettings = new QCheckBox(
        QStringLiteral("Also delete my settings, vocabulary and learned corrections"), &confirm);
    confirm.setCheckBox(deleteSettings);
    QPushButton *remove = confirm.addButton(QStringLiteral("Remove"), QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != remove) {
        return;
    }

    QStringList done;
    QStringList notDone;
    const DesktopIntegrationRemoval files = removeAppImageIntegration(QDir::homePath());
    for (const QString &item : files.removed) {
        done.append(QStringLiteral("Removed the %1.").arg(item));
    }
    for (const QString &item : files.absent) {
        done.append(QStringLiteral("There was no %1 to remove.").arg(item));
    }
    for (const QString &failure : files.failed) {
        notDone.append(QStringLiteral("Could not remove the %1.").arg(failure));
    }

    QString shortcutError;
    if (m_controller->removeGlobalShortcutRegistration(&shortcutError)) {
        done.append(QStringLiteral("Removed the Global Shortcut registration."));
    } else {
        notDone.append(shortcutError.isEmpty()
                           ? QStringLiteral("The Global Shortcut registration could not be removed.")
                           : QStringLiteral("Global Shortcut: %1").arg(shortcutError));
    }

    const bool deleteUserSettings = deleteSettings->isChecked();
    if (deleteUserSettings) {
        prepareForSettingsDeletion();
        if (m_controller->secretStore()->deleteKeyringApiKey()) {
            done.append(QStringLiteral("Deleted your API key from the desktop keyring."));
        } else {
            notDone.append(
                QStringLiteral("Could not delete your API key from the desktop keyring: %1")
                    .arg(m_controller->secretStore()->lastError()));
        }
        QSettings &raw = m_controller->settings()->raw();
        const QString settingsFile = raw.fileName();
        raw.clear();
        raw.sync();
        if (!QFile::exists(settingsFile) || QFile::remove(settingsFile)) {
            done.append(QStringLiteral("Deleted your settings."));
        } else {
            notDone.append(QStringLiteral("Could not delete the settings file at %1.").arg(settingsFile));
        }
    } else {
        done.append(QStringLiteral("Kept your settings."));
    }

    const QString appImage = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    QMessageBox report(window);
    report.setIcon(notDone.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    report.setWindowTitle(QStringLiteral("Remove Speecher"));
    report.setText(notDone.isEmpty() ? QStringLiteral("Speecher has been removed from this computer.")
                                     : QStringLiteral("Speecher was removed, with some things left to do by hand."));
    QString details = done.join(QLatin1Char('\n'));
    if (!notDone.isEmpty()) {
        details += QStringLiteral("\n\n") + notDone.join(QLatin1Char('\n'));
    }
    details += appImage.isEmpty()
        ? QStringLiteral("\n\nDelete the Speecher program file yourself to finish.")
        : QStringLiteral("\n\nTo finish, quit Speecher and delete the file at:\n%1").arg(appImage);
    report.setInformativeText(details);
    QPushButton *quit = report.addButton(QStringLiteral("Quit Speecher"), QMessageBox::AcceptRole);
    if (!deleteUserSettings) {
        report.addButton(QStringLiteral("Close"), QMessageBox::RejectRole);
    }
    report.exec();
    if (deleteUserSettings || report.clickedButton() == quit) {
        m_controller->quitApplication();
    }
}
#endif

void SettingsPageSet::updateAccessibilityState(bool supported, bool enabled, bool persistent)
{
    Q_UNUSED(persistent);
    m_targetAccessibility = supported && enabled;
    applyCapabilities();
}

void SettingsPageSet::applyCapabilities()
{
    const Capabilities capabilities{m_targetAccessibility,
                                    m_controller->updates()->supportsAutomaticDownloads(),
                                    Theme::overrideHonored()};
    m_general->setCapabilities(capabilities);
    m_output->setCapabilities(capabilities);
    m_applications->setCapabilities(capabilities);
    m_refinement->setCapabilities(capabilities);
    m_corrections->setCapabilities(capabilities);
    m_whatsNew->setCapabilities(capabilities);
}

} // namespace speecher
