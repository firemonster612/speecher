#pragma once

#include "app/AppFrontEnd.h"
#include "core/ShortcutBinding.h"

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
    // Whether the page on screen shows its fallback section.
    bool fallbacksShownForTest() const;
    // Presses the section's suggestion; false while it offers none.
    bool pressFallbackSuggestionForTest();
    // Scrolls the page on screen to its fallback section, for a picture.
    void revealFallbacksForTest();
    bool captureForTest(const QString &path);
    // Scrolls the page to its end, for a capture of what lies below the fold.
    void scrollToEndForTest();
    // Brings the window forward and opens a shortcut row's recording dialog.
    void recordShortcutForTest(GlobalShortcutRole role);
    static QStringList welcomeCopyForTest();
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
