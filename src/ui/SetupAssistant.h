#pragma once

#ifdef SPEECHER_WITH_KASSISTANT
#include <KAssistantDialog>
#else
#include <QWizard>
#endif

namespace speecher {

class ApplicationController;
class FinishSetupPage;
class MicrophoneSetupPage;
class TextDeliverySetupPage;

#ifdef SPEECHER_WITH_KASSISTANT
class SetupAssistant final : public KAssistantDialog {
#else
class SetupAssistant final : public QWizard {
#endif
public:
    explicit SetupAssistant(ApplicationController *controller,
                            QWidget *parent = nullptr);

private:
    void skipSetup();
    void completeSetup();
    void updateActivePage(QWidget *page);

    ApplicationController *m_controller;
    MicrophoneSetupPage *m_microphonePage;
    TextDeliverySetupPage *m_deliveryPage;
    FinishSetupPage *m_finishPage;
    bool m_skipping = false;
};

} // namespace speecher
