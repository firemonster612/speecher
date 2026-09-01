#include "ui/settings/SettingsPageSet.h"

#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/Theme.h"

#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

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

SettingsRow *rowById(SettingsSchema &schema, const QString &id)
{
    for (SettingsPage &page : schema.pages) {
        for (SettingsSection &section : page.sections) {
            for (SettingsRow &row : section.rows) {
                if (row.id == id) {
                    return &row;
                }
            }
        }
    }
    qWarning().noquote() << "settings schema cannot find row" << id;
    return nullptr;
}

SettingsSchema settingsSchema(ApplicationController *controller)
{
    SettingsSchema schema = buildSettingsSchema(qtSchemaContext(
        *controller->platform(),
        *controller->providerRegistry(),
        controller->primaryOutputStatus()));
    UpdateController *updates = controller->updates();

    if (SettingsRow *check = rowById(schema, QStringLiteral("checkForUpdates"))) {
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
                && updates->state() != UpdateController::State::RestartPending;
        };
    }

    if (SettingsRow *version = rowById(schema, QStringLiteral("currentVersion"))) {
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

} // namespace

SettingsPageSet::SettingsPageSet(ApplicationController *controller, QWidget *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_schema(settingsSchema(controller))
    , m_outputRows(*controller->settings())
    , m_providerRows(*controller->settings(), *controller->secretStore())
    , m_general(addPage(QStringLiteral("general"), parent))
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
    connect(page, &SchemaSettingsPage::changed, this, &SettingsPageSet::changed);
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

void SettingsPageSet::load()
{
    loadBeforeShow();
    loadAfterShow();
}

void SettingsPageSet::loadBeforeShow()
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        page->load(snapshot);
    }
    m_outputRows.refresh();
    refreshUpdateRows();
}

void SettingsPageSet::loadAfterShow()
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
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

    AppSettings draft = settings->snapshot();
    for (const SchemaSettingsPage *page : std::as_const(m_pages)) {
        page->appendToDraft(draft);
    }
    settings->applySnapshot(draft);
    Theme::apply(settings->theme());
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

bool SettingsPageSet::hasChanges() const
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
    for (const SchemaSettingsPage *page : m_pages) {
        if (page->hasChanges(snapshot)) {
            return true;
        }
    }
    return m_providerRows.hasSecretChanges();
}

void SettingsPageSet::runPageAction(const QString &rowId)
{
    if (rowId == QStringLiteral("runSetup")) {
        m_controller->showSetupAssistant();
        return;
    }
    if (rowId == QStringLiteral("checkForUpdates")) {
        AppSettings draft = m_controller->settings()->snapshot();
        m_general->appendToDraft(draft);
        if (m_controller->updates()->state() == UpdateController::State::UpdateAvailable) {
            m_controller->updates()->updateNow();
        } else {
            m_controller->updates()->checkForUpdates(draft.updates.channel);
        }
    }
}

void SettingsPageSet::refreshUpdateRows()
{
    m_general->refresh();
}

void SettingsPageSet::updateAccessibilityState(bool supported, bool enabled, bool persistent)
{
    Q_UNUSED(persistent);
    const Capabilities capabilities{supported && enabled,
                                    m_controller->updates()->isAppImage()};
    m_general->setCapabilities(capabilities);
    m_output->setCapabilities(capabilities);
    m_applications->setCapabilities(capabilities);
    m_refinement->setCapabilities(capabilities);
    m_corrections->setCapabilities(capabilities);
}

} // namespace speecher
