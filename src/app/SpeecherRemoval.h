#pragma once

#include "platform/LinuxDesktopIntegration.h"

#include <QObject>
#include <QStringList>

#include <functional>

namespace speecher {

class ApplicationController;

// What removing Speecher did: what came off, by name ("app menu entry",
// "virtual keyboard setup"), and each thing that did not, in core's words.
// The program file always stays, since a running AppImage can't safely
// delete itself; programFile is where it is, when Speecher runs from one.
struct RemovalReport {
    QStringList removed;
    QStringList problems;
    bool settingsDeleted = false;
    QString programFile;
};

// Undoes everything Speecher set up for this user on Linux, then reports it
// item by item: the desktop integration, the Global Shortcut registration,
// the system helpers it installed (each through pkexec, off the calling
// thread), and, when asked, the settings and keyring secrets. A step that
// fails does not stop the rest.
class SpeecherRemoval final : public QObject {
    Q_OBJECT

public:
    // The steps, so tests can stand in for the desktop, pkexec and the store.
    // The helper steps run off the calling thread; installed checks and the
    // rest run on it.
    struct Steps {
        std::function<DesktopIntegrationRemoval()> removeDesktopIntegration;
        std::function<bool(QString *error)> removeShortcutRegistration;
        std::function<bool()> virtualKeyboardInstalled;
        std::function<bool(QString *error)> stopVirtualKeyboard;
        std::function<bool(QString *error)> removeVirtualKeyboard;
        std::function<bool()> keyHelperInstalled;
        std::function<bool(QString *error)> removeKeyHelper;
        std::function<bool(QString *error)> deleteSecrets;
        std::function<bool(QString *error)> deleteSettings;
        QString programFile;
    };

    static Steps linuxSteps(ApplicationController *controller);

    explicit SpeecherRemoval(Steps steps, QObject *parent = nullptr);

    // Whether a removal is under way.
    bool running() const { return m_running; }
    // Starts a removal unless one is running; finished reports it.
    void start(bool deleteSettings);

signals:
    void finished(const RemovalReport &report);

private:
    void finish(RemovalReport report, bool deleteSettings);

    Steps m_steps;
    bool m_running = false;
};

} // namespace speecher
