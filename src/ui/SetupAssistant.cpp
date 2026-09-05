#include "ui/SetupAssistant.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/setup/SetupPages.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QAbstractButton>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef SPEECHER_WITH_KASSISTANT
#include <KPageWidgetItem>
#else
#include <QWizardPage>
#endif

namespace speecher {
namespace {

QStringList setupPageTitles()
{
    QStringList titles{
        QStringLiteral("Welcome to Speecher"),
        QStringLiteral("Transcription"),
        QStringLiteral("Microphone"),
        QStringLiteral("Desktop accessibility"),
        QStringLiteral("Text delivery"),
        QStringLiteral("Refinement"),
        QStringLiteral("Writing profiles"),
    };
#ifdef Q_OS_LINUX
    titles.append(QStringLiteral("Global Shortcut"));
#endif
    titles.append(QStringLiteral("Ready to dictate"));
    return titles;
}

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

SetupAssistant::SetupAssistant(ApplicationController *controller,
                               SetupAssistantPage page,
                               QWidget *parent)
#ifdef SPEECHER_WITH_KASSISTANT
    : KAssistantDialog(parent)
#else
    : QWizard(parent)
#endif
    , m_controller(controller)
    , m_singlePage(pageIndex(page) >= 0)
{
    setWindowTitle(QStringLiteral("Speecher Setup Assistant"));
    resize(720, 520);
    setMinimumSize(620, 460);

    const int requestedPageIndex = pageIndex(page);
#ifdef Q_OS_LINUX
    if (!m_singlePage || page == SetupAssistantPage::GlobalShortcut) {
        m_globalShortcutPage = new LinuxGlobalShortcutSetupPage(*controller, this);
        // The same control sits flush inside a settings card; as an assistant
        // page it takes the margin every other page has.
        const int margin = setupPageMargin();
        m_globalShortcutPage->layout()->setContentsMargins(margin, margin, margin, margin);
    }
#endif
    WelcomeSetupPage *welcome = nullptr;
    SpeechProviderSetupPage *speechProvider = nullptr;
    AccessibilitySetupPage *accessibility = nullptr;
    RefinementSetupPage *refinement = nullptr;
    if (!m_singlePage) {
        welcome = new WelcomeSetupPage(this);
        speechProvider = new SpeechProviderSetupPage(*controller->settings(),
                                                      *controller->providerRegistry(),
                                                      this);
        m_microphonePage = new MicrophoneSetupPage(*controller->settings(),
                                                   *controller->platform(),
                                                   this);
        accessibility = new AccessibilitySetupPage(*controller, this);
        m_deliveryPage = new TextDeliverySetupPage(*controller->settings(), this);
        refinement = new RefinementSetupPage(*controller->settings(),
                                             *controller->providerRegistry(),
                                             this);
        m_profilesPage = new WritingProfilesSetupPage(*controller->settings(), this);
        m_finishPage = new FinishSetupPage(*controller, this);
    }
    QList<QWidget *> pageContents{
        welcome,
        speechProvider,
        m_microphonePage,
        accessibility,
        m_deliveryPage,
        refinement,
        m_profilesPage,
    };
#ifdef Q_OS_LINUX
    pageContents.append(m_globalShortcutPage);
#endif
    pageContents.append(m_finishPage);
    if (!m_singlePage) {
        m_lastPage = pageContents.last();
    }
    const QStringList titles = setupPageTitles();
#ifdef SPEECHER_WITH_KASSISTANT
    for (int index = 0; index < pageContents.size(); ++index) {
        QWidget *content = pageContents.at(index);
        if (content && (requestedPageIndex < 0 || requestedPageIndex == index)) {
            addPage(content, titles.at(index));
        }
    }
    if (!m_singlePage) {
        m_skipButton = new QPushButton(QStringLiteral("Skip setup"), this);
        addActionButton(m_skipButton);
        connect(m_skipButton, &QAbstractButton::clicked, this, &SetupAssistant::skipSetup);
    }
    connect(this,
            &KAssistantDialog::currentPageChanged,
            this,
            [this](KPageWidgetItem *current, KPageWidgetItem *) {
                updateActivePage(current ? current->widget() : nullptr);
            });
#else
    // The platform's default wizard look, palette untouched: the separator it
    // draws is the style's own, not one tuned here.
    setOption(QWizard::NoBackButtonOnStartPage);
    if (!m_singlePage) {
        setOption(QWizard::HaveCustomButton1);
        setButtonText(QWizard::CustomButton1, QStringLiteral("Skip setup"));
        m_skipButton = button(QWizard::CustomButton1);
    }
    for (int index = 0; index < pageContents.size(); ++index) {
        QWidget *content = pageContents.at(index);
        if (content && (requestedPageIndex < 0 || requestedPageIndex == index)) {
            const int id = addPage(wizardPage(content, titles.at(index)));
            m_pageContents.insert(id, content);
        }
    }
    connect(this, &QWizard::customButtonClicked, this, [this](int button) {
        if (button == QWizard::CustomButton1) {
            skipSetup();
        }
    });
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        updateActivePage(m_pageContents.value(id, nullptr));
    });
#endif

    if (m_deliveryPage) {
        connect(m_deliveryPage,
                &TextDeliverySetupPage::signInRequirementChanged,
                m_finishPage,
                &FinishSetupPage::setSignInRequired);
    }
#ifdef Q_OS_LINUX
    if (m_singlePage) {
        updateActivePage(m_globalShortcutPage);
    } else {
        updateActivePage(welcome);
    }
#else
    updateActivePage(welcome);
#endif
}

QStringList SetupAssistant::pageTitles() const
{
    QStringList titles;
#ifdef SPEECHER_WITH_KASSISTANT
    const QAbstractItemModel *model = pageWidget()->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        titles.append(model->index(row, 0).data(Qt::DisplayRole).toString());
    }
#else
    for (const int id : pageIds()) {
        titles.append(page(id)->title());
    }
#endif
    return titles;
}

int SetupAssistant::pageIndex(SetupAssistantPage page)
{
    if (page == SetupAssistantPage::All) {
        return -1;
    }
#ifdef Q_OS_LINUX
    if (page == SetupAssistantPage::GlobalShortcut) {
        return setupPageTitles().indexOf(QStringLiteral("Global Shortcut"));
    }
#endif
    return -1;
}

void SetupAssistant::skipSetup()
{
    m_skipping = true;
    accept();
}

void SetupAssistant::accept()
{
    if (m_microphonePage) {
        m_microphonePage->setActive(false);
    }
    if (m_singlePage) {
#ifdef SPEECHER_WITH_KASSISTANT
        KAssistantDialog::accept();
#else
        QWizard::accept();
#endif
        return;
    }
    if (!m_skipping) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
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
    if (m_microphonePage) {
        m_microphonePage->setActive(page == m_microphonePage);
    }
    if (m_skipButton) {
        m_skipButton->setVisible(page != m_lastPage);
    }
    if (page == m_finishPage && m_finishPage) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
    }
}

} // namespace speecher
