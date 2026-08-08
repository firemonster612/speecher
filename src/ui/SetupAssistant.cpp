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
    auto *signIn = new ClaudeSignInSetupPage(*controller->settings(), this);
    auto *accessibility = new AccessibilitySetupPage(*controller, this);
    auto *refinement = new RefinementSetupPage(*controller->settings(),
                                               *controller->providerRegistry(),
                                               this);
    auto *profiles = new WritingProfilesSetupPage(*controller->settings(), this);

#ifdef SPEECHER_WITH_KASSISTANT
    addPage(welcome, QStringLiteral("Welcome"));
    addPage(signIn, QStringLiteral("Claude sign-in"));
    addPage(m_microphonePage, QStringLiteral("Microphone"));
    addPage(accessibility, QStringLiteral("Accessibility"));
    addPage(m_deliveryPage, QStringLiteral("Text delivery"));
    addPage(refinement, QStringLiteral("Refinement"));
    addPage(profiles, QStringLiteral("Writing profiles"));
    addPage(m_finishPage, QStringLiteral("Finish"));
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
    addPage(wizardPage(welcome, QStringLiteral("Welcome")));
    addPage(wizardPage(signIn, QStringLiteral("Claude sign-in")));
    addPage(wizardPage(m_microphonePage, QStringLiteral("Microphone")));
    addPage(wizardPage(accessibility, QStringLiteral("Accessibility")));
    addPage(wizardPage(m_deliveryPage, QStringLiteral("Text delivery")));
    addPage(wizardPage(refinement, QStringLiteral("Refinement")));
    addPage(wizardPage(profiles, QStringLiteral("Writing profiles")));
    addPage(wizardPage(m_finishPage, QStringLiteral("Finish")));
    connect(this, &QWizard::customButtonClicked, this, [this](int button) {
        if (button == QWizard::CustomButton1) {
            skipSetup();
        }
    });
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        QWizardPage *current = page(id);
        updateActivePage(current && current->layout()
                             ? current->layout()->itemAt(0)->widget()
                             : nullptr);
    });
#endif

    connect(this, &QDialog::accepted, this, &SetupAssistant::completeSetup);
    updateActivePage(welcome);
}

void SetupAssistant::skipSetup()
{
    m_skipping = true;
    m_controller->settings()->setSetupCompleted(true);
    accept();
}

void SetupAssistant::completeSetup()
{
    m_microphonePage->setActive(false);
    if (!m_skipping) {
        m_finishPage->applyShortcut();
        m_controller->settings()->setSetupCompleted(true);
    }
}

void SetupAssistant::updateActivePage(QWidget *page)
{
    m_microphonePage->setActive(page == m_microphonePage);
    if (page == m_finishPage) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
    }
}

} // namespace speecher
