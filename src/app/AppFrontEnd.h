#pragma once

#include "dictation/PopupPresentation.h"

#include <QString>
#include <QStringList>

namespace speecher {

enum class SetupAssistantPage {
    All,
    GlobalShortcut,
};

// The windows ApplicationController asks the user interface to show. The UI
// runs inside the daemon process, so the controller keeps the session,
// permission and IPC responsibilities and holds no windows; see
// docs/adr/0009-one-quick-ui-inside-the-daemon.md. With none attached, the
// daemon runs without windows and these requests do nothing.
class AppFrontEnd {
public:
    virtual ~AppFrontEnd() = default;

    virtual void showMainWindow() = 0;
    // Takes the main window off screen again, for when opened files arrive
    // just after a plain launch put it up by default.
    virtual void hideMainWindow() = 0;
    virtual void showSettingsWindow() = 0;
    virtual void showSetupAssistant(SetupAssistantPage page) = 0;

    // Shows the file transcription surface with these files listed, not yet
    // started.
    virtual void showTranscribeFiles(const QStringList &paths) = 0;

    // Shows a dictation failure the controller raised itself rather than the
    // session, such as the refused microphone grant, with what fixes it.
    virtual void showDictationError(const QString &message, const PopupErrorAction &fix = {}) = 0;

    // The system's attention sound.
    virtual void alert() = 0;

    // A system notification for news that arrives while no Speecher window is
    // on screen, such as a Local Model download finishing after setup closed.
    // Clicking it opens the settings window on pageId. A UI with a window up
    // already shows the news there and does nothing.
    virtual void notifyIfNoWindowShown(const QString &title, const QString &message, const QString &pageId) = 0;
};

} // namespace speecher
