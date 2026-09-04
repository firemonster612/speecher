#pragma once

#include "app/AppFrontEnd.h"

#ifdef SPEECHER_WITH_KASSISTANT
#include <KAssistantDialog>
#else
#include <QHash>
#include <QWizard>
#endif
#include <QStringList>

namespace speecher {

class ApplicationController;
class AccessibilitySetupPage;
class FinishSetupPage;
class MicrophoneSetupPage;
#ifdef Q_OS_LINUX
class LinuxGlobalShortcutSetupPage;
#endif
#ifdef Q_OS_MACOS
class StartAtLoginSetupPage;
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

    ApplicationController *m_controller;
    AccessibilitySetupPage *m_accessibilityPage = nullptr;
    MicrophoneSetupPage *m_microphonePage = nullptr;
    TextDeliverySetupPage *m_deliveryPage = nullptr;
    WritingProfilesSetupPage *m_profilesPage = nullptr;
    FinishSetupPage *m_finishPage = nullptr;
    bool m_singlePage = false;
#ifdef Q_OS_LINUX
    LinuxGlobalShortcutSetupPage *m_globalShortcutPage = nullptr;
#endif
#ifdef Q_OS_MACOS
    StartAtLoginSetupPage *m_startAtLoginPage = nullptr;
#endif
#ifndef SPEECHER_WITH_KASSISTANT
    QHash<int, QWidget *> m_pageContents;
#endif
    bool m_skipping = false;
};

} // namespace speecher
