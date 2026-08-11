#include "ui/settings/SettingsPageSet.h"

#include "app/ApplicationController.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "ui/Theme.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/AudioSettingsPage.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/ProviderSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

namespace speecher {

SettingsPageSet::SettingsPageSet(ApplicationController *controller, QWidget *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_general(new GeneralSettingsPage(controller->primaryOutputStatus(), parent))
    , m_audio(new AudioSettingsPage(*controller->platform(), parent))
    , m_applications(new ApplicationSettingsPage(parent))
    , m_output(new OutputSettingsPage(*controller->settings(), parent))
    , m_refinement(new RefinementSettingsPage(*controller->providerRegistry(), parent))
    , m_providers(new ProviderSettingsPage(*controller->settings(), *controller->secretStore(), parent))
    , m_vocabulary(new VocabularySettingsPage(parent))
    , m_corrections(new CorrectionsSettingsPage(parent))
    , m_bindings(new BindingsSettingsPage(parent))
{
    connect(m_general, &GeneralSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_audio, &AudioSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_applications, &ApplicationSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_output, &OutputSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_refinement, &RefinementSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_providers, &ProviderSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_vocabulary, &VocabularySettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_corrections, &CorrectionsSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(m_bindings, &BindingsSettingsPage::changed, this, &SettingsPageSet::changed);
    connect(controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &SettingsPageSet::updateAccessibilityState);
    updateAccessibilityState(controller->accessibilitySupported(),
                             controller->accessibilityEnabled(),
                             controller->accessibilityPersistent());
}

GeneralSettingsPage *SettingsPageSet::general() const { return m_general; }
AudioSettingsPage *SettingsPageSet::audio() const { return m_audio; }
ApplicationSettingsPage *SettingsPageSet::applications() const { return m_applications; }
OutputSettingsPage *SettingsPageSet::output() const { return m_output; }
RefinementSettingsPage *SettingsPageSet::refinement() const { return m_refinement; }
ProviderSettingsPage *SettingsPageSet::providers() const { return m_providers; }
VocabularySettingsPage *SettingsPageSet::vocabulary() const { return m_vocabulary; }
CorrectionsSettingsPage *SettingsPageSet::corrections() const { return m_corrections; }
BindingsSettingsPage *SettingsPageSet::bindings() const { return m_bindings; }

void SettingsPageSet::load()
{
    loadBeforeShow();
    loadAfterShow();
}

void SettingsPageSet::loadBeforeShow()
{
    SettingsStore *settings = m_controller->settings();
    const AppSettings snapshot = settings->snapshot();
    m_general->load(snapshot);
    m_applications->load(snapshot);
    m_refinement->load(snapshot);
    m_providers->loadModels();
    m_output->load(snapshot);
    m_providers->loadAuthModes();
    m_vocabulary->load(settings->vocabularyEntries());
    m_bindings->load(settings->bindingRules());
    m_corrections->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    m_output->refreshControls();
}

void SettingsPageSet::loadAfterShow()
{
    {
        const QSignalBlocker blocker(m_audio);
        m_audio->load(m_controller->settings()->snapshot());
    }
    m_providers->loadSecret();
}

bool SettingsPageSet::save(bool showValidationErrors,
                           bool refreshPages,
                           SaveFailure *failure)
{
    if (failure) *failure = SaveFailure::None;
    SettingsStore *settings = m_controller->settings();
    QList<BindingRule> bindingRules;
    if (!m_bindings->validate(&bindingRules, showValidationErrors)) {
        if (failure) *failure = SaveFailure::InvalidReplacementRules;
        return false;
    }
    if (!m_output->validate(showValidationErrors)) {
        if (failure) *failure = SaveFailure::DuplicatePasteRuleIds;
        return false;
    }
    AppSettings draft = settings->snapshot();
    m_general->appendToDraft(draft);
    m_audio->appendToDraft(draft);
    m_applications->appendToDraft(draft);
    m_output->appendToDraft(draft);
    m_refinement->appendToDraft(draft);
    m_providers->appendToDraft(draft);
    m_corrections->appendToDraft(draft);
    settings->applySnapshot(draft);
    Theme::apply(settings->theme());
    m_providers->saveAuthModes();
    settings->setVocabularyEntries(m_vocabulary->entries());
    settings->setCorrectionLearningEnabled(m_corrections->learningEnabled());
    settings->setBindingRules(bindingRules);
    if (!m_providers->saveSecret()) {
        if (failure) *failure = SaveFailure::ProviderSecret;
        return false;
    }
    if (refreshPages) {
        load();
    } else {
        m_providers->loadModels();
        m_output->refreshControls();
    }
    return true;
}

void SettingsPageSet::preserveBindingScroll(QScrollArea *scroll)
{
    connect(m_bindings, &BindingsSettingsPage::preserveScrollRequested,
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
    const SettingsStore *settings = m_controller->settings();
    const AppSettings snapshot = settings->snapshot();
    return m_general->hasChanges(snapshot)
        || m_audio->hasChanges(snapshot)
        || m_applications->hasChanges(snapshot)
        || m_refinement->hasChanges(snapshot)
        || m_providers->hasModelChanges()
        || m_output->hasChanges(snapshot)
        || m_providers->hasAuthChanges()
        || m_vocabulary->hasChanges(settings->vocabularyEntries())
        || m_corrections->hasChanges(settings->correctionLearningEnabled(), settings->learnedCorrections())
        || m_bindings->hasChanges(settings->bindingRules());
}

void SettingsPageSet::updateAccessibilityState(bool supported, bool enabled, bool persistent)
{
    Q_UNUSED(persistent);
    const bool available = supported && enabled;
    m_output->setTargetAccessibilityAvailable(available);
    m_applications->setTargetAccessibilityAvailable(available);
    m_refinement->setTargetAccessibilityAvailable(available);
    m_corrections->setTargetAccessibilityAvailable(available);
}

} // namespace speecher
