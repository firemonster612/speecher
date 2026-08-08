#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/AppSettings.h"
#include "core/SettingsStore.h"
#include "ui/Theme.h"
#include "ui/AccessibilityNotice.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/AudioSettingsPage.h"
#include "ui/settings/BindingsSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/GeneralSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/ProviderSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/settings/VocabularySettingsPage.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QTimer>

namespace speecher {

using namespace settings;

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
{
    auto *scroll = new QScrollArea(this);
    setWindowTitle(QStringLiteral("Speecher Settings"));
    resize(980, 780);
    setMinimumSize(820, 620);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_accessibilityNotice = new AccessibilityNotice(this);
    root->addWidget(m_accessibilityNotice);

    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *correctionsSection = makeSectionLabel(QStringLiteral("Learned Corrections"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Replacements & snippets"), this);

    m_vocabularyPage = new VocabularySettingsPage(this);
    m_correctionsPage = new CorrectionsSettingsPage(this);
    m_bindingsPage = new BindingsSettingsPage(this);
    connect(m_bindingsPage, &BindingsSettingsPage::preserveScrollRequested, scroll,
            [this, scroll](bool rebuilding) {
                QScrollBar *scrollBar = scroll->verticalScrollBar();
                if (rebuilding) {
                    m_preservedScrollValue = scrollBar->value();
                    return;
                }
                const auto restore = [this, scroll] {
                    QScrollBar *bar = scroll->verticalScrollBar();
                    bar->setValue(qMin(m_preservedScrollValue, bar->maximum()));
                };
                restore();
                QTimer::singleShot(0, scroll, restore);
            });

    m_generalPage = new GeneralSettingsPage(m_controller->primaryOutputStatus(), this);
    m_audioPage = new AudioSettingsPage(*m_controller->platform(), this);
    m_applicationPage = new ApplicationSettingsPage(this);
    m_outputPage = new OutputSettingsPage(*m_controller->settings(), this);
    m_providerPage = new ProviderSettingsPage(*m_controller->settings(), *m_controller->secretStore(), this);
    m_refinementPage = new RefinementSettingsPage(*m_controller->providerRegistry(), this);

    auto *vocabularyPageLayout = makeSettingsPage(scroll);
    vocabularyPageLayout->addWidget(vocabularySection);
    vocabularyPageLayout->addWidget(m_vocabularyPage);
    vocabularyPageLayout->addWidget(correctionsSection);
    vocabularyPageLayout->addWidget(m_correctionsPage);
    vocabularyPageLayout->addWidget(bindingsSection);
    vocabularyPageLayout->addWidget(m_bindingsPage);
    vocabularyPageLayout->addStretch();

    const QList<QPair<QString, QString>> categories{
        {QStringLiteral("General"), QStringLiteral("preferences-system")},
        {QStringLiteral("Audio"), QStringLiteral("audio-input-microphone")},
        {QStringLiteral("Applications"), QStringLiteral("applications-system")},
        {QStringLiteral("Output"), QStringLiteral("edit-paste")},
        {QStringLiteral("Refinement"), QStringLiteral("document-edit")},
        {QStringLiteral("Providers"), QStringLiteral("network-server")},
        {QStringLiteral("Vocabulary"), QStringLiteral("tools-check-spelling")},
    };
    const QList<QWidget *> pages{
        m_generalPage,
        m_audioPage,
        m_applicationPage,
        m_outputPage,
        m_refinementPage,
        m_providerPage,
        scroll,
    };

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    QListWidget *categoriesWidget = nullptr;
    QStackedWidget *pagesWidget = nullptr;
    addPageContainer(bodyLayout,
                     categories,
                     pages,
                     &categoriesWidget,
                     &pagesWidget,
                     body);
    root->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *footer = new QFrame(this);
    footer->setFrameShape(QFrame::NoFrame);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    auto *runtimeStatus = new QLabel(
        QStringLiteral("Dictation: %1").arg(m_controller->stateName()),
        footer);
    footerLayout->addWidget(runtimeStatus);
    footerLayout->addStretch();
    footerLayout->addWidget(buttons);
    root->addWidget(footer);

    for (QLabel *label : findChildren<QLabel *>()) {
        if (label->objectName() == QStringLiteral("subsectionLabel")) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        } else if (label->objectName() == QStringLiteral("rowDescription")
                   || label->objectName() == QStringLiteral("noteText")) {
            label->setForegroundRole(QPalette::PlaceholderText);
        }
    }

    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        m_okButton = ok;
        ok->setDefault(true);
        ok->setAutoDefault(true);
        ok->setIcon(QIcon());
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(false);
        cancel->setIcon(QIcon());
    }
    if (QPushButton *apply = buttons->button(QDialogButtonBox::Apply)) {
        m_applyButton = apply;
        apply->setAutoDefault(false);
        apply->setIcon(QIcon());
    }

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this] {
        save();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_controller,
            &ApplicationController::statusChanged,
            runtimeStatus,
            [runtimeStatus](const QString &status) {
                runtimeStatus->setText(QStringLiteral("Dictation: %1").arg(status));
            });
    connect(m_generalPage, &GeneralSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_audioPage, &AudioSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_applicationPage, &ApplicationSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_correctionsPage, &CorrectionsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_outputPage, &OutputSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_providerPage, &ProviderSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_refinementPage, &RefinementSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_vocabularyPage, &VocabularySettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_bindingsPage, &BindingsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_accessibilityNotice, &AccessibilityNotice::enableRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_accessibilityNotice->showError(error);
        }
    });
    connect(m_controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &SettingsDialog::updateAccessibilityState);
    updateAccessibilityState(m_controller->accessibilitySupported(),
                             m_controller->accessibilityEnabled(),
                             m_controller->accessibilityPersistent());
    load();
}

void SettingsDialog::load()
{
    SettingsStore *settings = m_controller->settings();
    m_generalPage->load(settings->snapshot());
    m_audioPage->load(settings->snapshot());
    m_applicationPage->load(settings->snapshot());
    m_refinementPage->load(settings->snapshot());
    m_providerPage->loadModels();
    m_outputPage->load(settings->snapshot());
    m_providerPage->loadAuth();
    m_vocabularyPage->load(settings->vocabularyEntries());
    m_bindingsPage->load(settings->bindingRules());
    m_correctionsPage->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    m_outputPage->refreshControls();
    updateButtonState();
}

bool SettingsDialog::save()
{
    SettingsStore *settings = m_controller->settings();
    QList<BindingRule> bindingRules;
    if (!m_bindingsPage->validate(&bindingRules)) {
        return false;
    }
    if (!m_outputPage->validate()) {
        return false;
    }
    AppSettings draft = settings->snapshot();
    m_generalPage->appendToDraft(draft);
    m_audioPage->appendToDraft(draft);
    m_applicationPage->appendToDraft(draft);
    m_outputPage->appendToDraft(draft);
    m_refinementPage->appendToDraft(draft);
    m_providerPage->appendToDraft(draft);
    m_correctionsPage->appendToDraft(draft);
    settings->applySnapshot(draft);
    Theme::apply(settings->theme());
    m_providerPage->loadModels();
    m_providerPage->saveAuthModes();
    settings->setVocabularyEntries(m_vocabularyPage->entries());
    settings->setCorrectionLearningEnabled(m_correctionsPage->learningEnabled());
    settings->setBindingRules(bindingRules);
    m_vocabularyPage->load(settings->vocabularyEntries());
    m_bindingsPage->load(settings->bindingRules());
    m_correctionsPage->load(settings->correctionLearningEnabled(), settings->learnedCorrections());
    if (!m_providerPage->saveSecret()) {
        return false;
    }
    m_outputPage->refreshControls();
    updateButtonState();
    return true;
}

bool SettingsDialog::hasChanges() const
{
    const SettingsStore *settings = m_controller->settings();
    if (m_generalPage->hasChanges(settings->snapshot())
        || m_audioPage->hasChanges(settings->snapshot())
        || m_applicationPage->hasChanges(settings->snapshot())
        || m_refinementPage->hasChanges(settings->snapshot())
        || m_providerPage->hasModelChanges()
        || m_outputPage->hasChanges(settings->snapshot())
        || m_providerPage->hasAuthChanges()
        || m_vocabularyPage->hasChanges(settings->vocabularyEntries())
        || m_correctionsPage->hasChanges(settings->correctionLearningEnabled(), settings->learnedCorrections())
        || m_bindingsPage->hasChanges(settings->bindingRules())) {
        return true;
    }

    return false;
}

void SettingsDialog::updateButtonState()
{
    const bool changed = hasChanges();
    if (m_okButton) {
        m_okButton->setEnabled(changed);
    }
    if (m_applyButton) {
        m_applyButton->setEnabled(changed);
    }
}

void SettingsDialog::updateAccessibilityState(bool supported,
                                              bool enabled,
                                              bool persistent)
{
    m_accessibilityNotice->setState(supported, enabled, persistent);
    const bool available = supported && enabled;
    m_outputPage->setTargetAccessibilityAvailable(available);
    m_applicationPage->setTargetAccessibilityAvailable(available);
    m_refinementPage->setTargetAccessibilityAvailable(available);
    m_correctionsPage->setTargetAccessibilityAvailable(available);
}

} // namespace speecher
