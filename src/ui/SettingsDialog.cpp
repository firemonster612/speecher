#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/Theme.h"
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
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace speecher {

using namespace settings;

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_scroll(new QScrollArea(this))
{
    setWindowTitle(QStringLiteral("Speecher Settings"));
    resize(980, 780);
    setMinimumSize(820, 620);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);
    auto *correctionsSection = makeSectionLabel(QStringLiteral("Learned Corrections"), this);
    auto *bindingsSection = makeSectionLabel(QStringLiteral("Replacements & snippets"), this);

    m_vocabularyPage = new VocabularySettingsPage(this);
    m_correctionsPage = new CorrectionsSettingsPage(this);
    m_bindingsPage = new BindingsSettingsPage(m_scroll, this);

    m_generalPage = new GeneralSettingsPage(m_controller->primaryOutputStatus(), this);
    m_audioPage = new AudioSettingsPage(*m_controller->platform(), this);
    m_outputPage = new OutputSettingsPage(*m_controller->settings(), this);
    m_providerPage = new ProviderSettingsPage(*m_controller->settings(), *m_controller->secretStore(), this);
    m_refinementPage = new RefinementSettingsPage(*m_controller->providerRegistry(), this);

    auto *vocabularyPageLayout = makeSettingsPage(m_scroll);
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
        {QStringLiteral("Output"), QStringLiteral("edit-paste")},
        {QStringLiteral("Refinement"), QStringLiteral("document-edit")},
        {QStringLiteral("Providers"), QStringLiteral("network-server")},
        {QStringLiteral("Vocabulary"), QStringLiteral("tools-check-spelling")},
    };
    const QList<QWidget *> pages{
        m_generalPage,
        m_audioPage,
        m_outputPage,
        m_refinementPage,
        m_providerPage,
        m_scroll,
    };

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    addPageContainer(bodyLayout,
                     categories,
                     pages,
                     &m_categories,
                     &m_pages,
                     body);
    root->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *footer = new QFrame(this);
    footer->setFrameShape(QFrame::NoFrame);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    m_runtimeStatus = new QLabel(
        QStringLiteral("Dictation: %1").arg(m_controller->stateName()),
        footer);
    footerLayout->addWidget(m_runtimeStatus);
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
            m_runtimeStatus,
            [this](const QString &status) {
                m_runtimeStatus->setText(QStringLiteral("Dictation: %1").arg(status));
            });
    connect(m_generalPage, &GeneralSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_audioPage, &AudioSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_correctionsPage, &CorrectionsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_outputPage, &OutputSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_providerPage, &ProviderSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_refinementPage, &RefinementSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_vocabularyPage, &VocabularySettingsPage::changed, this, &SettingsDialog::updateButtonState);
    connect(m_bindingsPage, &BindingsSettingsPage::changed, this, &SettingsDialog::updateButtonState);
    load();
}

void SettingsDialog::load()
{
    SettingsStore *settings = m_controller->settings();
    m_generalPage->load(settings->snapshot());
    m_audioPage->load(settings->snapshot());
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
    if (!m_refinementPage->validate()) {
        return false;
    }

    AppSettings draft = settings->snapshot();
    m_generalPage->appendToDraft(draft);
    m_audioPage->appendToDraft(draft);
    m_outputPage->appendToDraft(draft);
    m_refinementPage->appendToDraft(draft);
    settings->setTheme(draft.ui.theme);
    Theme::apply(settings->theme());
    settings->setPauseMediaDuringTranscription(draft.ui.pauseMediaDuringTranscription);
    settings->setSoundsEnabled(draft.ui.soundsEnabled);
    settings->setPreviewWords(draft.ui.previewWords);
    settings->setAudioCaptureSettings(draft.audio);
    settings->setRefinementProvider(draft.refinement.providerId);
    settings->setDefaultWritingProfile(draft.refinement.defaultWritingProfile);
    settings->setWritingProfileSettings(draft.refinement.writingProfiles);
    settings->setWritingProfileOverrides(draft.refinement.writingProfileOverrides);
    settings->setUseTargetContext(draft.refinement.useTargetContext);
    settings->setIncludeScreenshotContext(draft.refinement.includeScreenshotContext);
    m_providerPage->saveModels();
    settings->setOutputMethod(draft.output.method);
    settings->setOutputFormat(draft.output.format);
    settings->setPasteRules(draft.output.pasteRules);
    settings->setRestoreClipboardAfterTyping(draft.output.restoreClipboardAfterTyping);
    m_providerPage->saveAuthModes();
    settings->setVocabularyEntries(m_vocabularyPage->entries());
    settings->setCorrectionLearningEnabled(m_correctionsPage->learningEnabled());
    settings->setLearnedCorrections(m_correctionsPage->corrections());
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

} // namespace speecher
