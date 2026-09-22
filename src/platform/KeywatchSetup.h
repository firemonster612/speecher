#pragma once

#include <QObject>
#include <QString>

namespace speecher {

// The key-watch helper's installation, as the app can observe it. Mirrors
// YdotoolSetup: a probe gathers facts, evaluate() turns them into a state.
enum class KeywatchSetupState {
    NotInstalled,
    DaemonNotRunning,
    // The daemon runs and answers, but with another protocol version: an app
    // update outlived the installed helper. Only a reinstall repairs it.
    NeedsReinstall,
    Ready,
};

struct KeywatchProbeFacts {
    bool socketUnitInstalled = false;
    bool socketExists = false;
    bool socketWritable = false;
    // systemd's socket unit accepts a connection before the daemon has
    // started, so the files above prove only that the installation exists.
    // This is the daemon's own answer on that socket.
    bool daemonAnswers = false;
    // Whether that answer spoke this build's protocol version. An answer
    // alone proves liveness, not compatibility.
    bool daemonProtocolMatches = false;
};

struct KeywatchSetupStatus {
    KeywatchSetupState state = KeywatchSetupState::NotInstalled;
    QString label;
    QString detail;

    bool ready() const { return state == KeywatchSetupState::Ready; }
};

// Announces that a background liveness exchange finished, so a view that
// showed the previous answer can probe again. Lives on the thread that first
// asked, which is the GUI thread.
class KeywatchDaemonAnswer : public QObject {
    Q_OBJECT
signals:
    void changed();
};

class KeywatchSetup {
public:
    static KeywatchSetupStatus evaluate(const KeywatchProbeFacts &facts);
    // Never blocks: the daemon's liveness comes from the cached answer of the
    // last background exchange, and a stale one starts the next exchange.
    static KeywatchSetupStatus probe();
    static KeywatchDaemonAnswer *daemonAnswer();
    // Drops the cached daemon answer so the next probe asks the daemon again.
    // Install and remove call this themselves; a caller that changed the
    // helper some other way calls it to see the result without waiting.
    static void forgetDaemonAnswer();
    static bool install(QString *error = nullptr);
    static bool remove(QString *error = nullptr);
};

} // namespace speecher
