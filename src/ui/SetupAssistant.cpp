#include "ui/SetupAssistant.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/setup/SetupPages.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QAbstractButton>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
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

// The assistant's frame is fixed, so a page with more rows than fit scrolls
// rather than squeezing them into overlapping slivers. The wrapper is
// parented to the assistant immediately: KPageStackedWidget only adopts a
// page when it is shown, and until then a parentless wrapper would leave the
// page outside the assistant's object tree and without an owner.
QScrollArea *scrollingPage(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

// The page content behind whatever the wizard handed back.
QWidget *pageContent(QWidget *widget)
{
    auto *scroll = qobject_cast<QScrollArea *>(widget);
    return scroll ? scroll->widget() : widget;
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
    // Tall enough for the largest page (Transcription with its Sign-in card)
    // without a scrollbar, which narrows the cards and clips their statuses.
    resize(720, 640);
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
    AccessibilitySetupPage *accessibility = nullptr;
    RefinementSetupPage *refinement = nullptr;
    if (!m_singlePage) {
        m_welcomePage = new WelcomeSetupPage(*controller->settings(),
                                             *controller->providerRegistry(),
                                             this);
        m_speechProviderPage = new SpeechProviderSetupPage(*controller->settings(),
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
    // microphone or provider produced installs that never worked. Refinement
    // and writing profiles stay open, since their defaults are valid answers.
    if (m_welcomePage) {
        addGate(m_welcomePage, [this] { return m_welcomePage->ready(); });
        connect(m_welcomePage, &WelcomeSetupPage::readyChanged,
                this, [this] { applyGates(); });
    }
    if (m_speechProviderPage) {
        addGate(m_speechProviderPage, [this] { return m_speechProviderPage->ready(); });
        connect(m_speechProviderPage, &SpeechProviderSetupPage::readyChanged,
                this, [this] { applyGates(); });
    }
    if (m_microphonePage) {
        addGate(m_microphonePage, [this] { return m_microphonePage->inputDetected(); });
        connect(m_microphonePage, &MicrophoneSetupPage::inputDetectedChanged,
                this, [this] { applyGates(); });
    }
    if (accessibility) {
        addGate(accessibility, [accessibility] { return accessibility->stepComplete(); });
        connect(accessibility, &AccessibilitySetupPage::stepCompleteChanged,
                this, [this] { applyGates(); });
    }
    if (m_deliveryPage) {
        addGate(m_deliveryPage, [this] { return m_deliveryPage->stepComplete(); });
        connect(m_deliveryPage, &TextDeliverySetupPage::stepCompleteChanged,
                this, [this] { applyGates(); });
    }
#ifdef Q_OS_LINUX
    if (m_globalShortcutPage) {
        addGate(m_globalShortcutPage, [this] { return m_globalShortcutPage->stepComplete(); });
        connect(m_globalShortcutPage, &LinuxGlobalShortcutSetupPage::stepCompleteChanged,
                this, [this] { applyGates(); });
    }
#endif
    if (m_finishPage) {
        // The Ready page is only ready if every earlier step still is. A
        // credential that lapsed while the user walked the wizard must disable
        // Finish, not surface as a bounce-back after they press it.
        addGate(m_finishPage, [this] { return earlierGatesComplete(m_finishPage); });
    }

    QList<QWidget *> pageContents{
        m_welcomePage,
        m_speechProviderPage,
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
            KPageWidgetItem *item = addPage(scrollingPage(content, this), titles.at(index));
            if (m_gates.contains(content)) {
                m_gateItems.insert(content, item);
            }
            m_steps.append({titles.at(index), content});
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
                QWidget *content = current ? pageContent(current->widget()) : nullptr;
                updateActivePage(content);
                // setValid keeps a snapshot; a step completed outside this
                // dialog (the Output settings row, say) must reopen Next when
                // the user navigates.
                applyGates();
                if (content == m_finishPage) {
                    recheckCredentialsInBackground();
                }
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
            const int id = addPage(wizardPage(page, scrollingPage(content, this), titles.at(index)));
            m_pageContents.insert(id, content);
            m_steps.append({titles.at(index), content});
        }
    }
    connect(this, &QWizard::customButtonClicked, this, [this](int button) {
        if (button == QWizard::CustomButton1) {
            skipSetup();
        }
    });
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        QWidget *content = m_pageContents.value(id, nullptr);
        updateActivePage(content);
        // Steps can complete outside this dialog (the Output settings row,
        // say); navigating re-reads every gate.
        applyGates();
        if (content == m_finishPage) {
            recheckCredentialsInBackground();
        }
    });
#endif

    if (m_deliveryPage) {
        connect(m_deliveryPage,
                &TextDeliverySetupPage::signInRequirementChanged,
                m_finishPage,
                &FinishSetupPage::setSignInRequired);
    }
    if (m_finishPage) {
        connect(m_finishPage, &FinishSetupPage::stepSelected, this, [this](int index) {
            if (index >= 0 && index < m_finishStepPages.size()) {
                showPage(m_finishStepPages.at(index));
            }
        });
    }
    // Each page says where it sits in the run, which the wizard is the only
    // thing that knows. A single-page run has no run to count.
    if (!m_singlePage) {
        for (int index = 0; index < m_steps.size(); ++index) {
            setSetupStepCounter(m_steps.at(index).content, index + 1, m_steps.size());
        }
    }
    applyGates();
#ifdef Q_OS_LINUX
    if (m_singlePage) {
        updateActivePage(m_globalShortcutPage);
    } else {
        updateActivePage(m_welcomePage);
    }
#else
    updateActivePage(m_welcomePage);
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

void SetupAssistant::addGate(QWidget *content, std::function<bool()> gate)
{
    m_gates.insert(content, std::move(gate));
    m_gateOrder.append(content);
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
    updateFinishSteps();
    updateActivePage(m_activePage);
}

void SetupAssistant::updateFinishSteps()
{
    if (!m_finishPage) {
        return;
    }
    QList<SetupStepStatus> steps;
    m_finishStepPages.clear();
    for (const Step &step : m_steps) {
        // The Ready page is not one of the steps it reports on.
        if (step.content == m_finishPage) {
            continue;
        }
        const auto gate = m_gates.value(step.content);
        const bool ok = !gate || gate();
        QString detail;
        if (const auto *reporter = dynamic_cast<const SetupStep *>(step.content)) {
            detail = ok ? reporter->readySummary() : reporter->blockedReason();
        }
        steps.append({step.title, ok, detail});
        m_finishStepPages.append(step.content);
    }
    m_finishPage->setSteps(steps);
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

bool SetupAssistant::earlierGatesComplete(QWidget *content) const
{
    for (QWidget *page : m_gateOrder) {
        if (page == content) {
            return true;
        }
        const auto gate = m_gates.value(page);
        if (gate && !gate()) {
            return false;
        }
    }
    return true;
}

// The probes run off the GUI thread and report back through readyChanged,
// which already re-applies the gates. accept() must never wait on the network,
// so this only ever updates the gates for the next press of Finish.
void SetupAssistant::recheckCredentialsInBackground()
{
    if (!m_welcomePage) {
        if (m_speechProviderPage) {
            m_speechProviderPage->recheck();
        }
        return;
    }
    // Both pages probe the same provider objects, and a provider's credential
    // refresh holds a lock for a second. Run at the same time, one round
    // reports a lock failure the other caused and the gate closes on a
    // conflict rather than on the credentials, so the speech round waits.
    if (m_speechProviderPage) {
        connect(
            m_welcomePage,
            &WelcomeSetupPage::checkFinished,
            this,
            [this] {
                if (m_speechProviderPage) {
                    m_speechProviderPage->recheck();
                }
            },
            Qt::SingleShotConnection);
    }
    m_welcomePage->recheck();
}

QWidget *SetupAssistant::firstIncompletePage() const
{
    for (QWidget *content : m_gateOrder) {
        const auto gate = m_gates.value(content);
        if (gate && !gate()) {
            return content;
        }
    }
    return nullptr;
}

void SetupAssistant::showPage(QWidget *content)
{
#ifdef SPEECHER_WITH_KASSISTANT
    if (KPageWidgetItem *item = m_gateItems.value(content)) {
        setCurrentPage(item);
    }
#else
    const int targetId = m_pageContents.key(content, -1);
    if (targetId < 0) {
        return;
    }
    for (int steps = pageIds().size(); steps > 0 && currentId() != targetId; --steps) {
        back();
    }
#endif
}

void SetupAssistant::skipSetup()
{
    m_skipping = true;
    accept();
}

void SetupAssistant::accept()
{
    if (!m_singlePage) {
        // A step can break after its page was passed — the ydotool daemon
        // stopped, credentials expired — and the gates only ever governed the
        // page the user was on.
        if (QWidget *incomplete = firstIncompletePage()) {
            m_skipping = false;
            showPage(incomplete);
            applyGates();
            return;
        }
    }
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
