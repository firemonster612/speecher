#pragma once

#include <QString>

namespace speecher {

// The key-watch helper's installation, as the app can observe it. Mirrors
// YdotoolSetup: a probe gathers facts, evaluate() turns them into a state.
enum class KeywatchSetupState {
    NotInstalled,
    DaemonNotRunning,
    Ready,
};

struct KeywatchProbeFacts {
    bool socketUnitInstalled = false;
    bool socketExists = false;
    bool socketWritable = false;
};

struct KeywatchSetupStatus {
    KeywatchSetupState state = KeywatchSetupState::NotInstalled;
    QString label;
    QString detail;

    bool ready() const { return state == KeywatchSetupState::Ready; }
};

class KeywatchSetup {
public:
    static KeywatchSetupStatus evaluate(const KeywatchProbeFacts &facts);
    static KeywatchSetupStatus probe();
    static bool install(QString *error = nullptr);
    static bool remove(QString *error = nullptr);
};

} // namespace speecher
