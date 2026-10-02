#include "ui/SetupAssistant.h"

#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/SetupSteps.h"
#include "providers/ProviderRegistry.h"
#include "core/SettingsStore.h"
#include "ui/settings/SettingsPageSupport.h"
#include "ui/setup/SetupPages.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QAbstractButton>
#include <QEvent>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

#ifdef SPEECHER_WITH_KASSISTANT
#include <KPageWidget>
#include <QDialogButtonBox>
#include <KPageWidgetItem>
#include <KTitleWidget>
#include <QHBoxLayout>
#include <QLabel>
#else
#include <QWizardPage>
#endif

namespace speecher {
namespace {

QStringList setupPageTitles()
{
    QStringList titles;
    for (const SetupStepInfo &step : setupSteps()) {
        titles.append(step.title);
    }
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
    // The column every settings page has: capped, and centred in a wide
    // window rather than stretched across it.
    settings::configurePageScroll(scroll, content);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    setWindowTitle(setupWindowTitle());
    // Tall enough for the largest page (Transcription with its Sign-in card)
    // without a scrollbar, which narrows the cards and clips their statuses.
    resize(720, 640);
    setMinimumSize(620, 460);

#ifdef Q_OS_LINUX
    if (!m_singlePage || page == SetupAssistantPage::GlobalShortcut) {
        m_globalShortcutPage = new LinuxGlobalShortcutSetupPage(*controller, this);
        // The same control sits flush inside a settings card; as an assistant
        // page it takes the margin every other page has.
        const int margin = setupPageMargin();
        auto *layout = qobject_cast<QVBoxLayout *>(m_globalShortcutPage->layout());
        layout->setContentsMargins(margin, margin, margin, margin);
        // The settings card needs no lead; the assistant step has one.
        auto *intro = new QLabel(findSetupStep(QStringLiteral("shortcut"))->intro, m_globalShortcutPage);
        intro->setWordWrap(true);
        layout->insertWidget(0, intro);
    }
#endif
    AccessibilitySetupPage *accessibility = nullptr;
    RefinementSetupPage *refinement = nullptr;
    if (!m_singlePage) {
        // Speech on this computer is only a path where the build can run it.
        LocalSetup *localSpeech = controller->providerRegistry()->speechProvider(QStringLiteral("local"))
            ? controller->localSetup()
            : nullptr;
        m_welcomePage = new WelcomeSetupPage(this);
        m_speechProviderPage = new SpeechProviderSetupPage(*controller->settings(),
                                                           *controller->providerRegistry(),
                                                           localSpeech,
                                                           this);
        m_microphonePage = new MicrophoneSetupPage(*controller->settings(),
                                                   *controller->platform(),
                                                   this);
        accessibility = new AccessibilitySetupPage(*controller, this);
        // Text delivery is a step only where there is a virtual keyboard to
        // install.
        if (findSetupStep(QStringLiteral("delivery"))) {
            m_deliveryPage = new TextDeliverySetupPage(*controller->settings(), this);
        }
        refinement = new RefinementSetupPage(*controller->settings(),
                                             *controller->providerRegistry(),
                                             controller->localSetup(),
                                             this);
        m_finishPage = new FinishSetupPage(*controller, this);
    }
    // Every step with something checkable holds Next until it is done, and
    // Skip setup only exists once all of them are: skipping through an unset
    // microphone or provider produced installs that never worked. Refinement
    // stays open, since its default is a valid answer.
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

    // Core decides which steps this platform has; each page joins under its
    // step's title. A single-page run built only its own page, and a step
    // with no page here (the Global Shortcut off Linux) is left out.
    const QHash<QString, QWidget *> stepPages{
        {QStringLiteral("welcome"), m_welcomePage},
        {QStringLiteral("transcription"), m_speechProviderPage},
        {QStringLiteral("microphone"), m_microphonePage},
        {QStringLiteral("accessibility"), accessibility},
        {QStringLiteral("delivery"), m_deliveryPage},
        {QStringLiteral("refinement"), refinement},
#ifdef Q_OS_LINUX
        {QStringLiteral("shortcut"), m_globalShortcutPage},
#endif
        {QStringLiteral("ready"), m_finishPage},
    };
    for (const SetupStepInfo &step : setupSteps()) {
        if (QWidget *content = stepPages.value(step.id)) {
            m_steps.append({step.title, content});
        }
    }
    m_lastPage = m_finishPage;
#ifdef SPEECHER_WITH_KASSISTANT
    for (const Step &step : std::as_const(m_steps)) {
        KPageWidgetItem *item = addPage(scrollingPage(step.content, this), step.title);
        step.content->installEventFilter(this);
        // The title row below draws the title, beside the counter.
        item->setHeaderVisible(false);
        m_items.append(item);
        if (m_gates.contains(step.content)) {
            m_gateItems.insert(step.content, item);
        }
    }
    // The title row carries the counter, as the other two assistants do:
    // KPageView takes a header widget in place of the title it draws.
    auto *header = new QWidget(this);
    m_header = header;
    auto *headerLayout = new QHBoxLayout(header);
    m_headerTitle = new KTitleWidget(header);
    headerLayout->addWidget(m_headerTitle, 1);
    m_headerCounter = new QLabel(header);
    m_headerCounter->setObjectName(QStringLiteral("setupStepCounter"));
    m_headerCounter->setForegroundRole(QPalette::PlaceholderText);
    m_headerCounter->setVisible(!m_singlePage);
    headerLayout->addWidget(m_headerCounter, 0, Qt::AlignRight | Qt::AlignVCenter);
    pageWidget()->setPageHeader(header);
    updateStepHeader(m_items.value(0));
    // KPageDialog offers Help, and Speecher has no help pages behind it.
    // Enter presses Next or Finish only: while a gate holds Next, no other
    // button becomes the default in its place.
    if (QPushButton *help = buttonBox()->button(QDialogButtonBox::Help)) {
        help->hide();
    }
    backButton()->setAutoDefault(false);
    if (QPushButton *cancel = buttonBox()->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(false);
    }
    if (!m_singlePage) {
        m_skipButton = new QPushButton(setupText(SetupText::SkipSetup), this);
        addActionButton(m_skipButton);
        connect(m_skipButton, &QAbstractButton::clicked, this, &SetupAssistant::skipSetup);
    }
    connect(this,
            &KAssistantDialog::currentPageChanged,
            this,
            [this](KPageWidgetItem *current, KPageWidgetItem *) {
                QWidget *content = current ? pageContent(current->widget()) : nullptr;
                updateStepHeader(current);
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
        setButtonText(QWizard::CustomButton1, setupText(SetupText::SkipSetup));
        m_skipButton = button(QWizard::CustomButton1);
    }
    for (const Step &step : std::as_const(m_steps)) {
        QWizardPage *page = nullptr;
        if (m_gates.contains(step.content)) {
            auto *gated = new GatedWizardPage;
            gated->gate = m_gates.value(step.content);
            m_gatePages.insert(step.content, gated);
            page = gated;
        } else {
            page = new QWizardPage;
        }
        const int id = addPage(wizardPage(page, scrollingPage(step.content, this), step.title));
        m_pageContents.insert(id, step.content);
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
#ifndef SPEECHER_WITH_KASSISTANT
    // Each page says where it sits in the run, which the wizard is the only
    // thing that knows. A single-page run has no run to count.
    if (!m_singlePage) {
        const QList<int> ids = pageIds();
        for (int index = 0; index < ids.size(); ++index) {
            QWizard::page(ids.at(index))->setSubTitle(setupStepCounter(index + 1, ids.size()));
        }
    }
#endif
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

#ifdef SPEECHER_WITH_KASSISTANT
void SetupAssistant::updateStepHeader(KPageWidgetItem *current)
{
    if (!current) {
        return;
    }
    m_headerTitle->setText(current->name());
    m_headerCounter->setText(setupStepCounter(m_items.indexOf(current) + 1, m_items.size()));
}

void SetupAssistant::alignStepHeader()
{
    if (!m_activePage || !m_activePage->isVisible()) {
        return;
    }
    const int left = m_activePage->mapTo(this, QPoint()).x() - m_header->mapTo(this, QPoint()).x();
    const int right = m_header->width() - left - m_activePage->width();
    const QMargins margins = m_header->layout()->contentsMargins();
    m_header->layout()->setContentsMargins(left + setupPageMargin(), margins.top(),
                                           right + setupPageMargin(), margins.bottom());
}

bool SetupAssistant::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_activePage
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize
            || event->type() == QEvent::Show)) {
        alignStepHeader();
    }
    return KAssistantDialog::eventFilter(watched, event);
}
#endif

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
        SetupStepStatus status{step.title, ok};
        if (const auto *reporter = dynamic_cast<const SetupStep *>(step.content)) {
            status.detail = ok ? reporter->readySummary() : reporter->blockedReason();
            status.localModelId = reporter->localModelId();
            status.verdict = reporter->readyVerdict();
            status.verdictReady = reporter->readyVerdictPositive();
        }
        steps.append(status);
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
    if (m_speechProviderPage) {
        m_speechProviderPage->recheck();
    }
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
        m_finishPage->setSignInRequired(m_deliveryPage && m_deliveryPage->needsSignIn());
    }
    m_controller->completeSetup();
#ifdef SPEECHER_WITH_KASSISTANT
    KAssistantDialog::accept();
#else
    QWizard::accept();
#endif
}

void SetupAssistant::updateActivePage(QWidget *page)
{
    m_activePage = page;
#ifdef SPEECHER_WITH_KASSISTANT
    alignStepHeader();
#endif
    if (m_microphonePage) {
        m_microphonePage->setActive(page == m_microphonePage);
    }
    if (m_skipButton) {
        // Skip is only a shortcut past pages whose steps are already done,
        // never a way around them.
        m_skipButton->setVisible(page != m_lastPage && gatesComplete());
    }
    if (page == m_finishPage && m_finishPage) {
        m_finishPage->setSignInRequired(m_deliveryPage && m_deliveryPage->needsSignIn());
    }
}

} // namespace speecher
