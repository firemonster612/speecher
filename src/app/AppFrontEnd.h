#pragma once

#include <QString>

namespace speecher {

enum class SetupAssistantPage {
    All,
    GlobalShortcut,
};

// Everything ApplicationController needs from whichever user interface is
// compiled in. One implementation per platform, so that the controller keeps
// the session, permission and IPC responsibilities and holds no widgets; see
// docs/adr/0001-per-platform-front-ends.md.
class AppFrontEnd {
public:
    virtual ~AppFrontEnd() = default;

    virtual void showMainWindow() = 0;
    virtual void showSettingsWindow() = 0;
    // The settings window open at a named page, from `speecher settings
    // <page>`. A front end without named pages shows its settings as usual.
    virtual void showSettingsPage(const QString &pageId)
    {
        Q_UNUSED(pageId)
        showSettingsWindow();
    }
    virtual void showSetupAssistant(SetupAssistantPage page) = 0;

    // Saves a picture of the main window, for the --grab screenshot path.
    // False when there is no main window yet or the file could not be written.
    virtual bool captureMainWindow(const QString &path) = 0;

    // Shows a dictation failure the controller raised itself rather than the
    // session, which is only the refused microphone grant.
    virtual void showDictationError(const QString &message) = 0;

    // The system's attention sound.
    virtual void alert() = 0;
};

} // namespace speecher
