#pragma once

#include "app/AppFrontEnd.h"

#include <functional>
#include <memory>

#include <QObject>
#include <QStringList>

namespace speecher {

class ApplicationController;
class WinFrontEndTests;

class SetupWindow final : public QObject {
public:
    explicit SetupWindow(ApplicationController *controller,
                         std::function<void()> firstFrame,
                         QObject *parent = nullptr);
    ~SetupWindow() override;

    void show(SetupAssistantPage page);
    bool isVisible() const;
    static QStringList pageTitles();

private:
    friend class WinFrontEndTests;
    void skipForTest();
    QString currentPageTitleForTest() const;
    void showPageForTest(const QString &stepId);
    bool finishEnabledForTest() const;
    bool captureForTest(const QString &path);
    static QStringList welcomeCopyForTest();
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
