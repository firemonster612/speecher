#pragma once

#ifdef SPEECHER_WITH_KASSISTANT
#include <KAssistantDialog>
#else
#include <QHash>
#include <QWizard>
#endif

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
                            QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    void skipSetup();
    void updateActivePage(QWidget *page);

    ApplicationController *m_controller;
    AccessibilitySetupPage *m_accessibilityPage;
    MicrophoneSetupPage *m_microphonePage;
    TextDeliverySetupPage *m_deliveryPage;
    WritingProfilesSetupPage *m_profilesPage;
    FinishSetupPage *m_finishPage;
#ifdef Q_OS_LINUX
    LinuxGlobalShortcutSetupPage *m_globalShortcutPage;
#endif
#ifdef Q_OS_MACOS
    StartAtLoginSetupPage *m_startAtLoginPage;
#endif
#ifndef SPEECHER_WITH_KASSISTANT
    QHash<int, QWidget *> m_pageContents;
#endif
    bool m_skipping = false;
};

} // namespace speecher
