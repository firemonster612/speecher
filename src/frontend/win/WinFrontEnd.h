#pragma once

#include "app/AppFrontEnd.h"

#include <memory>

#include <QObject>

namespace speecher {

class ApplicationController;
class WinFrontEndTests;
class WinUiHost;
class WinFrontEnd final : public QObject, public AppFrontEnd {
public:
    WinFrontEnd(ApplicationController *controller, std::unique_ptr<WinUiHost> host);
    ~WinFrontEnd() override;

    void showMainWindow() override;
    void showSettingsWindow() override;
    void showSetupAssistant(SetupAssistantPage page) override;
    bool captureMainWindow(const QString &path) override;
    void showDictationError(const QString &message) override;
    void alert() override;

private:
    friend class WinFrontEndTests;
    void showPanelForTest(quint64 generation);
    void dismissPanelForTest();
    bool panelVisibleForTest() const;
    quint64 panelPresentedGenerationForTest() const;
    qintptr panelWindowStyleForTest() const;
    void actionTriggered(const QString &rowId);
    void reportReady();

    struct Native;

    ApplicationController *m_controller;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
