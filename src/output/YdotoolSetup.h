#pragma once

#include <QString>

namespace speecher {

enum class YdotoolSetupState {
    Disabled,
    NotInstalled,
    DaemonNotRunning,
    NeedsUinputPermission,
    NeedsSignOut,
    Ready,
    ReadyLayoutCaveat,
};

struct YdotoolProbeFacts {
    bool enabledInSpeecher = false;
    bool ydotoolInstalled = false;
    bool ydotooldInstalled = false;
    bool uinputExists = false;
    bool uinputReadWrite = false;
    bool speecherGroupExists = false;
    // Speecher's service file or setup record is on the computer.
    bool speecherSetupFilesInstalled = false;
    bool userInConfiguredGroup = false;
    // Another account is in the group, so the shared files are also theirs.
    bool otherAccountsInConfiguredGroup = false;
    bool currentSessionInConfiguredGroup = false;
    bool socketExists = false;
    bool socketWritable = false;
};

struct YdotoolSetupStatus {
    YdotoolSetupState state = YdotoolSetupState::NotInstalled;
    QString label;
    QString detail;
    // Speecher's setup is installed for this account. Files that stay for
    // other accounts after this one left the group do not count.
    bool speecherManagedSetupInstalled = false;

    bool ready() const { return state == YdotoolSetupState::Ready || state == YdotoolSetupState::ReadyLayoutCaveat; }
};

class YdotoolSetup {
public:
    enum class HelperAction {
        Install,
        Remove,
    };

    static YdotoolSetupStatus evaluate(const YdotoolProbeFacts &facts);
    static YdotoolSetupStatus probe(bool enabledInSpeecher);
    static QString serviceName();
    static bool runHelper(HelperAction action, QString *error = nullptr);
    static bool startUserService(QString *error = nullptr);
    static bool stopUserService(QString *error = nullptr);
};

} // namespace speecher
