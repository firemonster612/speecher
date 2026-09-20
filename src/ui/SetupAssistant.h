#pragma once

#include "app/AppFrontEnd.h"

#ifdef SPEECHER_WITH_KASSISTANT
#include <KAssistantDialog>
#else
#include <QWizard>
#endif
#include <QHash>
#include <QList>
#include <QStringList>

#include <functional>

class QAbstractButton;
#ifdef SPEECHER_WITH_KASSISTANT
class KPageWidgetItem;
#else
class QWizardPage;
#endif

namespace speecher {

class ApplicationController;
class FinishSetupPage;
class MicrophoneSetupPage;
class SpeechProviderSetupPage;
class WelcomeSetupPage;
#ifdef Q_OS_LINUX
class LinuxGlobalShortcutSetupPage;
#endif
class TextDeliverySetupPage;
class WritingProfilesSetupPage;

#ifdef SPEECHER_WITH_KASSISTANT
class SetupAssistant final : public KAssistantDialog {
#else
class SetupAssistant final : public QWizard {
#endif
public:
    explicit SetupAssistant(ApplicationController *controller,
                            SetupAssistantPage page = SetupAssistantPage::All,
                            QWidget *parent = nullptr);
    QStringList pageTitles() const;

protected:
    void accept() override;

private:
    static int pageIndex(SetupAssistantPage page);
    void skipSetup();
    void updateActivePage(QWidget *page);
    void addGate(QWidget *content, std::function<bool()> gate);
    void applyGates();
    bool gatesComplete() const;
    // Whether every gate ahead of `content` in wizard order is satisfied.
    bool earlierGatesComplete(QWidget *content) const;
    QWidget *firstIncompletePage() const;
    void showPage(QWidget *content);
    void recheckCredentialsInBackground();

    // Pages whose step must be completed before Next (and, while any is
    // incomplete, before Skip setup is offered at all).
    QHash<QWidget *, std::function<bool()>> m_gates;
    // The same pages in wizard order, so a failed finish can return to the
    // first step that broke rather than an arbitrary one.
    QList<QWidget *> m_gateOrder;
#ifdef SPEECHER_WITH_KASSISTANT
    QHash<QWidget *, KPageWidgetItem *> m_gateItems;
#else
    QHash<QWidget *, QWizardPage *> m_gatePages;
#endif

    ApplicationController *m_controller;
    WelcomeSetupPage *m_welcomePage = nullptr;
    SpeechProviderSetupPage *m_speechProviderPage = nullptr;
    MicrophoneSetupPage *m_microphonePage = nullptr;
    TextDeliverySetupPage *m_deliveryPage = nullptr;
    WritingProfilesSetupPage *m_profilesPage = nullptr;
    FinishSetupPage *m_finishPage = nullptr;
    QWidget *m_lastPage = nullptr;
    QWidget *m_activePage = nullptr;
    QAbstractButton *m_skipButton = nullptr;
    bool m_singlePage = false;
#ifdef Q_OS_LINUX
    LinuxGlobalShortcutSetupPage *m_globalShortcutPage = nullptr;
#endif
#ifndef SPEECHER_WITH_KASSISTANT
    QHash<int, QWidget *> m_pageContents;
#endif
    bool m_skipping = false;
};

} // namespace speecher
