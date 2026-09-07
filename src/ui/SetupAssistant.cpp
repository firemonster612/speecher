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

#include <functional>

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
// Holds the wizard's Next button until the gate opens: QWizard re-reads
// isComplete() whenever completeChanged() fires.
class GatedWizardPage final : public QWizardPage {
public:
    std::function<bool()> gate;

    bool isComplete() const override
    {
        return (!gate || gate()) && QWizardPage::isComplete();
    }

    void refreshGate() { emit completeChanged(); }
};

QWizardPage *wizardPage(QWizardPage *page, QWidget *content, const QString &title)
{
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
    // Every step with something checkable holds Next until it is done, and
    // Skip setup only exists once all of them are: skipping through an unset
    // microphone or provider produced installs that never worked. Welcome,
    // refinement, and writing profiles stay open, since their defaults are
    // valid answers.
    if (speechProvider) {
        m_gates.insert(speechProvider,
                       [speechProvider] { return speechProvider->ready(); });
        connect(speechProvider, &SpeechProviderSetupPage::readyChanged,
                this, [this] { applyGates(); });
    }
    if (m_microphonePage) {
        m_gates.insert(m_microphonePage,
                       [this] { return m_microphonePage->inputDetected(); });
        connect(m_microphonePage, &MicrophoneSetupPage::inputDetectedChanged,
                this, [this] { applyGates(); });
    }
    if (accessibility) {
        m_gates.insert(accessibility,
                       [accessibility] { return accessibility->stepComplete(); });
        connect(accessibility, &AccessibilitySetupPage::stepCompleteChanged,
                this, [this] { applyGates(); });
    }
    if (m_deliveryPage) {
        m_gates.insert(m_deliveryPage,
                       [this] { return m_deliveryPage->stepComplete(); });
        connect(m_deliveryPage, &TextDeliverySetupPage::stepCompleteChanged,
                this, [this] { applyGates(); });
    }
#ifdef Q_OS_LINUX
    if (m_globalShortcutPage) {
        m_gates.insert(m_globalShortcutPage,
                       [this] { return m_globalShortcutPage->stepComplete(); });
        connect(m_globalShortcutPage, &LinuxGlobalShortcutSetupPage::stepCompleteChanged,
                this, [this] { applyGates(); });
    }
#endif

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
            KPageWidgetItem *item = addPage(content, titles.at(index));
            if (m_gates.contains(content)) {
                m_gateItems.insert(content, item);
            }
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
                // setValid keeps a snapshot; a step completed outside this
                // dialog (the Output settings row, say) must reopen Next when
                // the user navigates.
                applyGates();
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
            QWizardPage *page = nullptr;
            if (m_gates.contains(content)) {
                auto *gated = new GatedWizardPage;
                gated->gate = m_gates.value(content);
                m_gatePages.insert(content, gated);
                page = gated;
            } else {
                page = new QWizardPage;
            }
            const int id = addPage(wizardPage(page, content, titles.at(index)));
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
        // Steps can complete outside this dialog (the Output settings row,
        // say); navigating re-reads every gate.
        applyGates();
    });
#endif

    if (m_deliveryPage) {
        connect(m_deliveryPage,
                &TextDeliverySetupPage::signInRequirementChanged,
                m_finishPage,
                &FinishSetupPage::setSignInRequired);
    }
    applyGates();
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

void SetupAssistant::applyGates()
{
    for (auto it = m_gates.cbegin(); it != m_gates.cend(); ++it) {
#ifdef SPEECHER_WITH_KASSISTANT
        if (KPageWidgetItem *item = m_gateItems.value(it.key())) {
            setValid(item, it.value()());
        }
#else
        if (QWizardPage *page = m_gatePages.value(it.key())) {
            static_cast<GatedWizardPage *>(page)->refreshGate();
        }
#endif
    }
    updateActivePage(m_activePage);
}

bool SetupAssistant::gatesComplete() const
{
    for (const auto &gate : m_gates) {
        if (!gate()) {
            return false;
        }
    }
    return true;
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
    m_activePage = page;
    if (m_microphonePage) {
        m_microphonePage->setActive(page == m_microphonePage);
    }
    if (m_skipButton) {
        // Skip is only a shortcut past pages whose steps are already done,
        // never a way around them.
        m_skipButton->setVisible(page != m_lastPage && gatesComplete());
    }
    if (page == m_finishPage && m_finishPage) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
    }
}

} // namespace speecher
