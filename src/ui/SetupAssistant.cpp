#include "ui/SetupAssistant.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/setup/SetupPages.h"

#include <QPushButton>
#include <QVBoxLayout>

#ifdef SPEECHER_WITH_KASSISTANT
#include <KPageWidgetItem>
#else
#include <QWizardPage>
#endif

namespace speecher {
namespace {

#ifndef SPEECHER_WITH_KASSISTANT
QWizardPage *wizardPage(QWidget *content, const QString &title)
{
    auto *page = new QWizardPage;
    page->setTitle(title);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
    return page;
}
#endif

} // namespace

SetupAssistant::SetupAssistant(ApplicationController *controller, QWidget *parent)
#ifdef SPEECHER_WITH_KASSISTANT
    : KAssistantDialog(parent)
#else
    : QWizard(parent)
#endif
    , m_controller(controller)
    , m_microphonePage(new MicrophoneSetupPage(*controller->settings(),
                                               *controller->platform(),
                                               this))
    , m_deliveryPage(new TextDeliverySetupPage(*controller->settings(), this))
    , m_finishPage(new FinishSetupPage(*controller, this))
{
    setWindowTitle(QStringLiteral("Speecher Setup Assistant"));
    resize(720, 520);
    setMinimumSize(620, 460);

    auto *welcome = new WelcomeSetupPage(this);
    auto *speechProvider = new SpeechProviderSetupPage(*controller->settings(),
                                                       *controller->providerRegistry(),
                                                       this);
    auto *accessibility = new AccessibilitySetupPage(*controller, this);
    auto *refinement = new RefinementSetupPage(*controller->settings(),
                                               *controller->providerRegistry(),
                                               this);
    auto *profiles = new WritingProfilesSetupPage(*controller->settings(), this);

#ifdef SPEECHER_WITH_KASSISTANT
    addPage(welcome, QStringLiteral("Welcome to Speecher"));
    addPage(speechProvider, QStringLiteral("Transcription"));
    addPage(m_microphonePage, QStringLiteral("Microphone"));
    addPage(accessibility, QStringLiteral("Desktop accessibility"));
    addPage(m_deliveryPage, QStringLiteral("Text delivery"));
    addPage(refinement, QStringLiteral("Refinement"));
    addPage(profiles, QStringLiteral("Writing profiles"));
    addPage(m_finishPage, QStringLiteral("Ready to dictate"));
    auto *skip = new QPushButton(QStringLiteral("Skip setup"), this);
    addActionButton(skip);
    connect(skip, &QPushButton::clicked, this, &SetupAssistant::skipSetup);
    connect(this,
            &KAssistantDialog::currentPageChanged,
            this,
            [this](KPageWidgetItem *current, KPageWidgetItem *) {
                updateActivePage(current ? current->widget() : nullptr);
            });
#else
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setOption(QWizard::HaveCustomButton1);
    setButtonText(QWizard::CustomButton1, QStringLiteral("Skip setup"));
    const auto addSetupPage = [this](QWidget *content, const QString &title) {
        const int id = addPage(wizardPage(content, title));
        m_pageContents.insert(id, content);
    };
    addSetupPage(welcome, QStringLiteral("Welcome to Speecher"));
    addSetupPage(speechProvider, QStringLiteral("Transcription"));
    addSetupPage(m_microphonePage, QStringLiteral("Microphone"));
    addSetupPage(accessibility, QStringLiteral("Desktop accessibility"));
    addSetupPage(m_deliveryPage, QStringLiteral("Text delivery"));
    addSetupPage(refinement, QStringLiteral("Refinement"));
    addSetupPage(profiles, QStringLiteral("Writing profiles"));
    addSetupPage(m_finishPage, QStringLiteral("Ready to dictate"));
    connect(this, &QWizard::customButtonClicked, this, [this](int button) {
        if (button == QWizard::CustomButton1) {
            skipSetup();
        }
    });
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        updateActivePage(m_pageContents.value(id, nullptr));
    });
#endif

    connect(m_deliveryPage,
            &TextDeliverySetupPage::signInRequirementChanged,
            m_finishPage,
            &FinishSetupPage::setSignInRequired);
    updateActivePage(welcome);
}

void SetupAssistant::skipSetup()
{
    m_skipping = true;
    accept();
}

void SetupAssistant::accept()
{
    m_microphonePage->setActive(false);
    if (!m_skipping) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
        if (!m_finishPage->applyShortcut()) {
            return;
        }
    }
    m_controller->settings()->setSetupCompleted(true);
#ifdef SPEECHER_WITH_KASSISTANT
    KAssistantDialog::accept();
#else
    QWizard::accept();
#endif
}

void SetupAssistant::updateActivePage(QWidget *page)
{
    m_microphonePage->setActive(page == m_microphonePage);
    if (page == m_finishPage) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
    }
}

} // namespace speecher
