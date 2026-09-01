#include "ui/SetupAssistant.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "ui/setup/SetupPages.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QColor>
#include <QMessageBox>
#include <QPalette>
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
    }
#endif
    WelcomeSetupPage *welcome = nullptr;
    SpeechProviderSetupPage *speechProvider = nullptr;
    RefinementSetupPage *refinement = nullptr;
    if (!m_singlePage) {
        welcome = new WelcomeSetupPage(this);
        speechProvider = new SpeechProviderSetupPage(*controller->settings(),
                                                      *controller->providerRegistry(),
                                                      this);
        m_microphonePage = new MicrophoneSetupPage(*controller->settings(),
                                                   *controller->platform(),
                                                   this);
        m_accessibilityPage = new AccessibilitySetupPage(*controller, this);
        m_deliveryPage = new TextDeliverySetupPage(*controller->settings(), this);
        refinement = new RefinementSetupPage(*controller->settings(),
                                             *controller->providerRegistry(),
                                             this);
        m_profilesPage = new WritingProfilesSetupPage(*controller->settings(), this);
        m_finishPage = new FinishSetupPage(*controller, this);
#ifdef Q_OS_MACOS
        m_startAtLoginPage = new StartAtLoginSetupPage(*controller->settings(), this);
#endif
    }
#ifdef SPEECHER_WITH_KASSISTANT
    int nextPageIndex = 0;
    const auto addSetupPage = [this, requestedPageIndex, &nextPageIndex](QWidget *content,
                                                                          const QString &title) {
        if (content && (requestedPageIndex < 0 || requestedPageIndex == nextPageIndex)) {
            addPage(content, title);
        }
        ++nextPageIndex;
    };
#ifdef Q_OS_LINUX
    addSetupPage(m_globalShortcutPage, QStringLiteral("Global Shortcut"));
#endif
    addSetupPage(welcome, QStringLiteral("Welcome to Speecher"));
    addSetupPage(speechProvider, QStringLiteral("Transcription"));
    addSetupPage(m_microphonePage, QStringLiteral("Microphone"));
    addSetupPage(m_accessibilityPage, QStringLiteral("Desktop accessibility"));
    addSetupPage(m_deliveryPage, QStringLiteral("Text delivery"));
    addSetupPage(refinement, QStringLiteral("Refinement"));
    addSetupPage(m_profilesPage, QStringLiteral("Writing profiles"));
    addSetupPage(m_finishPage, QStringLiteral("Ready to dictate"));
#ifdef Q_OS_MACOS
    addSetupPage(m_startAtLoginPage, QStringLiteral("Start at login"));
#endif
    if (!m_singlePage) {
        auto *skip = new QPushButton(QStringLiteral("Skip setup"), this);
        addActionButton(skip);
        connect(skip, &QPushButton::clicked, this, &SetupAssistant::skipSetup);
    }
    connect(this,
            &KAssistantDialog::currentPageChanged,
            this,
            [this](KPageWidgetItem *current, KPageWidgetItem *) {
                updateActivePage(current ? current->widget() : nullptr);
            });
#else
    setWizardStyle(QWizard::ClassicStyle);
    QPalette wizardPalette = palette();
    const QColor window = wizardPalette.color(QPalette::Window);
    const QColor text = wizardPalette.color(QPalette::WindowText);
    wizardPalette.setColor(QPalette::Mid,
                           QColor((window.red() * 4 + text.red()) / 5,
                                  (window.green() * 4 + text.green()) / 5,
                                  (window.blue() * 4 + text.blue()) / 5));
    setPalette(wizardPalette);
    setOption(QWizard::NoBackButtonOnStartPage);
    if (!m_singlePage) {
        setOption(QWizard::HaveCustomButton1);
        setButtonText(QWizard::CustomButton1, QStringLiteral("Skip setup"));
    }
    int nextPageIndex = 0;
    const auto addSetupPage = [this, requestedPageIndex, &nextPageIndex](QWidget *content,
                                                                          const QString &title) {
        if (content && (requestedPageIndex < 0 || requestedPageIndex == nextPageIndex)) {
            const int id = addPage(wizardPage(content, title));
            m_pageContents.insert(id, content);
        }
        ++nextPageIndex;
    };
#ifdef Q_OS_LINUX
    addSetupPage(m_globalShortcutPage, QStringLiteral("Global Shortcut"));
#endif
    addSetupPage(welcome, QStringLiteral("Welcome to Speecher"));
    addSetupPage(speechProvider, QStringLiteral("Transcription"));
    addSetupPage(m_microphonePage, QStringLiteral("Microphone"));
    addSetupPage(m_accessibilityPage, QStringLiteral("Desktop accessibility"));
    addSetupPage(m_deliveryPage, QStringLiteral("Text delivery"));
    addSetupPage(refinement, QStringLiteral("Refinement"));
    addSetupPage(m_profilesPage, QStringLiteral("Writing profiles"));
    addSetupPage(m_finishPage, QStringLiteral("Ready to dictate"));
#ifdef Q_OS_MACOS
    addSetupPage(m_startAtLoginPage, QStringLiteral("Start at login"));
#endif
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

int SetupAssistant::pageIndex(SetupAssistantPage page)
{
    if (page == SetupAssistantPage::All) {
        return -1;
    }
#ifdef Q_OS_LINUX
    if (page == SetupAssistantPage::GlobalShortcut) {
        return 0;
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
        if (!m_finishPage->applyShortcut()) {
            return;
        }
#ifdef Q_OS_MACOS
        m_startAtLoginPage->apply();
#endif
    }
    m_controller->settings()->setSetupCompleted(true);
    if (m_accessibilityPage->accessibilityGrantAppearedDuringSetup()) {
        QMessageBox::information(
            this,
            QStringLiteral("Accessibility granted"),
            QStringLiteral("Speecher will now restart to apply the Accessibility grant."),
            QMessageBox::Ok);
        m_controller->platform()->relaunch();
        return;
    }
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
    if (page == m_finishPage && m_finishPage) {
        m_finishPage->setSignInRequired(m_deliveryPage->needsSignIn());
    }
}

} // namespace speecher
