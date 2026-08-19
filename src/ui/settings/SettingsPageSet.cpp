#include "ui/settings/SettingsPageSet.h"

#include "app/ApplicationController.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/Theme.h"

#include <QDesktopServices>
#include <QMessageBox>
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

} // namespace

SettingsPageSet::SettingsPageSet(ApplicationController *controller, QWidget *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_schema(buildSettingsSchema(qtSchemaContext(*controller->platform(),
                                                   *controller->providerRegistry(),
                                                   controller->primaryOutputStatus())))
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
                           {QStringLiteral("openAiModel"),
                            QStringLiteral("openAiModelCaution"),
                            QStringLiteral("openAiEffort"),
                            QStringLiteral("anthropicModel"),
                            QStringLiteral("anthropicModelCaution"),
                            QStringLiteral("anthropicEffort")},
                           false),
          parent))
    , m_providerAuth(addPage(
          providerRowsPage(m_schema.page(QStringLiteral("providers")),
                           {QStringLiteral("openAiAuthMode"),
                            QStringLiteral("openAiCliproxyAccount"),
                            QStringLiteral("openAiAuth"),
                            QStringLiteral("anthropicAuthMode"),
                            QStringLiteral("anthropicCliproxyAccount"),
                            QStringLiteral("cliproxyBaseUrl"),
                            QStringLiteral("cliproxyApiKey")},
                           true),
          parent,
          m_providerRows.factory()))
{
    connect(controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &SettingsPageSet::updateAccessibilityState);
    updateAccessibilityState(controller->accessibilitySupported(),
                             controller->accessibilityEnabled(),
                             controller->accessibilityPersistent());
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
}

void SettingsPageSet::loadAfterShow()
{
    const AppSettings snapshot = m_controller->settings()->snapshot();
    for (SchemaSettingsPage *page : std::as_const(m_pages)) {
        const QSignalBlocker blocker(page);
        page->loadExpensiveRows(snapshot);
    }
    m_providerRows.loadSecret();
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
                      {QStringLiteral("Could not save provider credentials")});
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
    if (rowId == QStringLiteral("openReleases")) {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/firemonster612/speecher/releases")));
    }
}

void SettingsPageSet::updateAccessibilityState(bool supported, bool enabled, bool persistent)
{
    Q_UNUSED(persistent);
    const Capabilities capabilities{supported && enabled};
    m_output->setCapabilities(capabilities);
    m_applications->setCapabilities(capabilities);
    m_refinement->setCapabilities(capabilities);
    m_corrections->setCapabilities(capabilities);
}

} // namespace speecher
