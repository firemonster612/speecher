#include "ui/settings/SettingsPageSet.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PlatformComposition.h"
#include "app/UpdateBanner.h"
#include "app/UpdateController.h"
#include "core/InsightsSummary.h"
#include "core/AppSettings.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "frontend/qt/LocalModelRows.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "providers/LocalModelStore.h"
#include "providers/TranscriptRefinementPrompt.h"
#ifdef Q_OS_LINUX
#include "output/YdotoolSetup.h"
#include "platform/KeywatchSetup.h"
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
#include <QMediaDevices>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>

namespace speecher {

namespace {

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
    SchemaContext context = qtSchemaContext(*controller->platform(),
                                            *controller->providerRegistry(),
                                            controller->pendingWhatsNewVersion());
    context.liveFacts = [setup = controller->localSetup()] { return setup->liveFacts(); };
    context.liveFactsForDraft = [setup = controller->localSetup()](const AppSettings &draft) {
        return setup->liveFacts(draft);
    };
    SettingsSchema schema = buildSettingsSchema(context);
    UpdateController *updates = controller->updates();
    SettingsPage &general = pageById(schema, QStringLiteral("general"));

    bindCheckForUpdatesRow(schema, controller->updateBanner());

    if (SettingsRow *version = rowById(general, QStringLiteral("currentVersion"))) {
        version->value = [updates](const AppSettings &) {
            QString text = updates->currentVersion();
            if (!updates->availableVersion().isEmpty()) {
                text += QStringLiteral(" — %1 available").arg(updates->availableVersionDisplay());
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
        SchemaCustomRow row;
        row.widget = new LinuxGlobalShortcutSetupPage(
            *controller, parent, LinuxGlobalShortcutSetupPage::Placement::SettingsCard);
        row.cardRows = true;
        return row;
    };
#else
    // The Qt window runs only on Linux; the other front ends draw their own
    // recorder, so this build of it only has to stand the row in.
    Q_UNUSED(controller)
    return [](const SettingsRow &descriptor, QWidget *parent, std::function<void()>) {
        return descriptor.id == QStringLiteral("globalShortcut")
            ? SchemaCustomRow{new QWidget(parent), {}, {}, true}
            : SchemaCustomRow{};
    };
#endif
}

// Every front-end-supplied row, whichever pane shows it: each factory
// answers only for its own rows.
SchemaCustomRowFactory combinedRows(QList<SchemaCustomRowFactory> factories)
{
    factories.removeAll(nullptr);
    return [factories](const SettingsRow &descriptor, QWidget *parent, std::function<void()> notifyChanged) {
        for (const SchemaCustomRowFactory &factory : factories) {
            SchemaCustomRow row = factory(descriptor, parent, notifyChanged);
            if (row.widget) {
                return row;
            }
        }
        return SchemaCustomRow{};
    };
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
{
    const SchemaCustomRowFactory customRows = combinedRows({
        generalCustomRows(controller),
        m_outputRows.factory(),
        m_bindingRows.factory(),
        m_providerRows.factory(),
        m_schema.hasPage(QStringLiteral("localModels")) ? localModelRows(*controller->localSetup())
                                                        : SchemaCustomRowFactory(),
        whatsNewCustomRow,
    });
    for (const SettingsPane &pane : std::as_const(m_schema.panes)) {
        if (pane.layout == PaneLayout::Alternatives) {
            for (const SettingsPaneGroup &group : pane.groups) {
                // The view's tab already carries its title.
                SettingsSection section = m_schema.section(group);
                section.title.clear();
                addPage(pane.id + QLatin1Char(':') + group.view, {section}, parent, customRows);
            }
        } else if (!pane.groups.isEmpty()) {
            QList<SettingsSection> sections;
            for (const SettingsPaneGroup &group : pane.groups) {
                sections.append(m_schema.section(group));
            }
            addPage(pane.id, sections, parent, customRows);
        }
    }
    preserveScroll(page(QStringLiteral("vocabulary:replacements")));

    connect(controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &SettingsPageSet::updateAccessibilityState);
    connect(new QMediaDevices(this), &QMediaDevices::audioInputsChanged,
            this, &SettingsPageSet::refreshMicrophones);
    connect(controller->updateBanner(),
            &UpdateBanner::changed,
            this,
            &SettingsPageSet::refreshUpdateRows);
    connect(this, &SettingsPageSet::changed,
            this, &SettingsPageSet::refreshUpdateRows);
    // What LocalSetup learns shows up in rows on three pages: endpoint
    // verdicts, runners, model lists.
    connect(controller->localSetup(), &LocalSetup::changed, this, [this] {
        // LocalSetup writes Speed Test results and the model in use itself.
        const auto current = m_controller->settings()->dictationSnapshot();
        m_draft = mergeSettingsDraft(m_schema, m_loaded, m_draft, current);
        m_loaded = current;
        for (const QString &id : {QStringLiteral("dictation"), QStringLiteral("refinement"),
                                  QStringLiteral("localModels")}) {
            if (SchemaSettingsPage *live = page(id)) {
                const QSignalBlocker blocker(live);
                live->load(m_draft);
            }
        }
    });
    updateAccessibilityState(controller->accessibilitySupported(),
                             controller->accessibilityEnabled(),
                             controller->accessibilityPersistent());
    refreshUpdateRows();
}

void SettingsPageSet::addPage(const QString &id,
                              const QList<SettingsSection> &sections,
                              QWidget *parent,
                              const SchemaCustomRowFactory &customRows)
{
    auto *page = new SchemaSettingsPage(sections, parent, customRows);
    page->setObjectName(id);
    connect(page, &SchemaSettingsPage::changed, this, [this, page] {
        page->appendToDraft(m_draft);
        for (SchemaSettingsPage *candidate : std::as_const(m_pages)) {
            const QSignalBlocker blocker(candidate);
            candidate->load(m_draft);
        }
        emit changed();
    });
    connect(page, &SchemaSettingsPage::actionTriggered, this, &SettingsPageSet::runPageAction);
    m_pages.insert(id, page);
}

const SettingsSchema &SettingsPageSet::schema() const { return m_schema; }

SchemaSettingsPage *SettingsPageSet::page(const QString &id) const { return m_pages.value(id); }

void SettingsPageSet::load()
{
    loadBeforeShow();
    loadAfterShow();
}

void SettingsPageSet::loadBeforeShow()
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
    m_draft = m_loaded = snapshot;
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
    m_draft = m_loaded = snapshot;
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        const QSignalBlocker blocker(page);
        page->loadExpensiveRows(snapshot);
    }
    m_providerRows.loadSecret();
    refreshMicrophones();
    refreshUpdateRows();
    m_controller->localSetup()->probeHardware();
    m_controller->localSetup()->detectRunners();
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
    SchemaSettingsPage *replacements = page(QStringLiteral("vocabulary:replacements"));
    const QStringList replacementProblems = replacements->validate();
    if (!replacementProblems.isEmpty()) {
        return refuseAloud(SaveFailure::InvalidReplacementRules,
                           replacements,
                           QStringLiteral("Replacements not saved"),
                           replacementProblems);
    }
    SchemaSettingsPage *pasteRules = page(QStringLiteral("output"));
    const QStringList pasteRuleProblems = pasteRules->validate();
    if (!pasteRuleProblems.isEmpty()) {
        return refuseAloud(SaveFailure::DuplicatePasteRuleIds,
                           pasteRules,
                           QStringLiteral("Paste rules not saved"),
                           pasteRuleProblems);
    }
    SchemaSettingsPage *refinement = page(QStringLiteral("refinement"));
    const QStringList refinementProblems = refinement->validate();
    if (!refinementProblems.isEmpty()) {
        return refuseAloud(SaveFailure::InvalidTonesOrCleanupLevels,
                           refinement,
                           QStringLiteral("Tones and cleanup levels not saved"),
                           refinementProblems);
    }

    settings->applySnapshot(mergeSettingsDraft(m_schema, m_loaded, m_draft, settings->snapshot()));
    Theme::apply(settings->theme());
    const QString savedTheme = Theme::normalizedSetting(settings->theme(),
                                                        Theme::overrideHonored());
    if (savedTheme != settings->theme()) {
        m_draft.ui.theme = savedTheme;
        settings->setTheme(savedTheme);
        Theme::apply(savedTheme);
    }
    // Applying the theme is the moment that reveals whether the platform
    // honours it, which the Theme row's gate depends on.
    applyCapabilities();
    const AppSettings snapshot = settings->snapshot();
    m_draft = m_loaded = snapshot;
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

void SettingsPageSet::preserveScroll(QScrollArea *scroll)
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
    if (rowId == QStringLiteral("clearInsights")) {
        QMessageBox confirm(qobject_cast<QWidget *>(parent()));
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(homeText(HomeText::ClearHistoryTitle));
        confirm.setText(homeText(HomeText::ClearHistoryQuestion));
        confirm.setInformativeText(homeText(HomeText::ClearHistoryBody));
        QPushButton *remove = confirm.addButton(homeText(HomeText::ClearHistoryConfirm),
                                                QMessageBox::DestructiveRole);
        confirm.addButton(QMessageBox::Cancel);
        confirm.setDefaultButton(QMessageBox::Cancel);
        confirm.exec();
        if (confirm.clickedButton() == remove && !m_controller->clearInsights()) {
            QMessageBox::warning(qobject_cast<QWidget *>(parent()),
                                 homeText(HomeText::ClearHistoryTitle),
                                 homeText(HomeText::ClearHistoryFailed));
        }
        return;
    }
    if (rowId == QStringLiteral("resetCustomSystemPrompt")) {
        m_draft.refinement.customSystemPrompt = builtInDictationSystemPrompt();
        for (SchemaSettingsPage *candidate : std::as_const(m_pages)) {
            const QSignalBlocker blocker(candidate);
            candidate->load(m_draft);
        }
        emit changed();
        return;
    }
    if (rowId == QStringLiteral("refreshMicrophones")) {
        refreshMicrophones();
        page(QStringLiteral("dictation"))->loadExpensiveRows(m_draft);
        return;
    }
    if (rowId == QStringLiteral("checkForUpdates")) {
        m_controller->updateBanner()->runCheckRow(m_draft.updates.channel);
        return;
    }
    if (m_controller->localSetup()->runSettingsAction(rowId, m_draft)) {
        return;
    }
    if (rowId == QStringLiteral("whatsNew")) {
        m_controller->clearPendingWhatsNew();
        emit whatsNewRequested();
        return;
    }
    if (rowId == QStringLiteral("speechLocalModelDownload")) {
        emit localModelsRequested();
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
                                 m_schema.row(QStringLiteral("desktopAccessibility"))->label,
                                 error.isEmpty()
                                     ? QStringLiteral("Desktop accessibility could not be turned on.")
                                     : error);
        }
    }
}

void SettingsPageSet::refreshUpdateRows()
{
    page(QStringLiteral("general"))->refresh();
    page(QStringLiteral("whatsNew"))->refresh();
}

#ifdef Q_OS_LINUX
// One confirmation, then everything Speecher set up for this user is undone
// and the outcome is reported item by item. The program file itself stays: a
// running AppImage cannot delete itself safely, and the person knows where
// they put it.
void SettingsPageSet::removeSpeecher()
{
    QWidget *window = qobject_cast<QWidget *>(parent());
    // The privileged helpers are separate root installations. Removing only
    // this user's files would leave a system service, a device rule and a
    // group membership behind while the report claimed a clean removal.
    const bool ydotoolInstalled = YdotoolSetup::probe(false).speecherManagedSetupInstalled;
    const bool keywatchInstalled =
        KeywatchSetup::probe().state != KeywatchSetupState::NotInstalled;
    const bool anyHelperInstalled = ydotoolInstalled || keywatchInstalled;

    QMessageBox confirm(window);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(QStringLiteral("Remove Speecher"));
    confirm.setText(QStringLiteral("Remove Speecher from this computer?"));
    confirm.setInformativeText(
        QStringLiteral("This removes the app menu entry, the speecher command, the app icon and "
                       "the Global Shortcut registration. The Speecher program file stays where "
                       "you put it.")
        // Each helper is its own pkexec'd program, so an install with both of
        // them asks twice. Promising one prompt makes the second look wrong.
        + (anyHelperInstalled
               ? QStringLiteral("\n\nIt also removes the system helpers Speecher installed, which "
                                "asks for administrator permission for each helper. They are "
                                "shared by every account on this computer.")
               : QString()));
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

    if (ydotoolInstalled) {
        QString stopError;
        if (!YdotoolSetup::stopUserService(&stopError) && !stopError.isEmpty()) {
            notDone.append(QStringLiteral("Virtual keyboard service: %1").arg(stopError));
        }
        QString helperError;
        if (YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Remove, &helperError)) {
            done.append(QStringLiteral("Removed the virtual keyboard setup."));
        } else {
            notDone.append(helperError.isEmpty()
                               ? QStringLiteral("The virtual keyboard setup could not be removed.")
                               : QStringLiteral("Virtual keyboard: %1").arg(helperError));
        }
    }
    if (keywatchInstalled) {
        QString helperError;
        if (KeywatchSetup::remove(&helperError)) {
            done.append(QStringLiteral("Removed the single-key helper."));
        } else {
            notDone.append(helperError.isEmpty()
                               ? QStringLiteral("The single-key helper could not be removed.")
                               : QStringLiteral("Key helper: %1").arg(helperError));
        }
    }

    const bool deleteUserSettings = deleteSettings->isChecked();
    if (deleteUserSettings) {
        prepareForSettingsDeletion();
        if (m_controller->secretStore()->deleteKeyringSecrets()) {
            done.append(QStringLiteral("Deleted your API keys from the desktop keyring."));
        } else {
            notDone.append(
                QStringLiteral("Could not delete your API keys from the desktop keyring: %1")
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

void SettingsPageSet::refreshMicrophones()
{
    m_audioInput = !m_controller->platform()->availableAudioInputDevices().isEmpty();
    applyCapabilities();
}

Capabilities SettingsPageSet::capabilities() const
{
    Capabilities capabilities;
    capabilities.targetAccessibility = m_targetAccessibility;
    capabilities.automaticUpdateDownloads = m_controller->updates()->supportsAutomaticDownloads();
    capabilities.colorSchemeOverride = Theme::overrideHonored();
    capabilities.audioInput = m_audioInput;
    return capabilities;
}

void SettingsPageSet::applyCapabilities()
{
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        page->setCapabilities(capabilities());
    }
}

QList<SearchMatch> SettingsPageSet::searchSettings(const QString &query) const
{
    return speecher::searchSettings(m_schema, query, m_draft, capabilities());
}

} // namespace speecher
