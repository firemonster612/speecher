#pragma once

#include "platform/LinuxDesktopIntegration.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <functional>
#include <optional>

namespace speecher {

class ApplicationController;

// One part of Speecher that removal took off, or tried to: the Global
// Shortcut registration, the virtual keyboard's service and setup, the
// single-key helper, the API keys in the keyring, or the settings. error is
// why a step failed, in the words of the part that failed, or empty where it
// said nothing; for settings, it is the file that would not go.
struct RemovalStep {
    enum class Outcome {
        Removed,
        Failed,
        // Settings the person chose to keep.
        Kept,
        // Settings deleted all but the deletion markers that hide API keys
        // the keyring couldn't delete: deleting those would show the keys.
        KeptHidingKeys,
    };
    enum class Part {
        ShortcutRegistration,
        VirtualKeyboardService,
        VirtualKeyboard,
        KeyHelper,
        Keyring,
        Settings,
    };
    Part part;
    Outcome outcome;
    QString error;
};

// What removing Speecher did: the desktop's files item by item, then each
// other part in the order removal took them. The program file always stays,
// since a running AppImage can't safely delete itself; programFile is where
// it is, when Speecher runs from one.
struct RemovalReport {
    DesktopIntegrationRemoval files;
    QList<RemovalStep> steps;
    QString programFile;

    bool settingsDeleted() const;
};

// Undoes everything Speecher set up for this user on Linux, then reports it
// item by item: the desktop integration, the Global Shortcut registration,
// the system helpers it installed (each through pkexec, off the calling
// thread), and, when asked, the keyring secrets and the settings. Those go
// last, so no later step writes them again, the settings after the keyring's
// secrets and all but what hides a key the keyring couldn't delete. Any other
// step that fails does not stop the rest.
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
        // Whether the settings hold a deletion marker, which hides a key the
        // keyring kept (SecretStore::deletionMarkers).
        std::function<bool()> settingsHideKeys;
        // On failure, the paths of the settings files that stayed in error.
        std::function<bool(QString *error)> deleteSettings;
        // Deletes every setting but the deletion markers; on failure, the
        // paths of the settings files that stayed in error.
        std::function<bool(QString *error)> deleteSettingsButMarkers;
        QString programFile;
    };

    // This computer's steps: Linux's. Elsewhere none, as Speecher sets up
    // nothing outside its own app, which goes the platform's way: the Trash,
    // or Windows' uninstaller.
    static std::optional<Steps> onThisComputer(ApplicationController *controller);

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
