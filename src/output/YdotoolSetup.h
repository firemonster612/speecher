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
    bool speecherManagedSetupInstalled = false;
    bool userInConfiguredGroup = false;
    bool currentSessionInConfiguredGroup = false;
    bool socketExists = false;
    bool socketWritable = false;
};

struct YdotoolSetupStatus {
    YdotoolSetupState state = YdotoolSetupState::NotInstalled;
    QString label;
    QString detail;
    bool speecherManagedSetupInstalled = false;

    bool ready() const;
    bool canEnable() const;
};

class YdotoolSetup {
public:
    enum class HelperAction {
        Install,
        Remove,
    };

    static YdotoolSetupStatus evaluate(const YdotoolProbeFacts &facts);
    static YdotoolSetupStatus probe(bool enabledInSpeecher);
    static QString stateId(YdotoolSetupState state);
    static QString serviceName();
    static QString helperPath();
    static bool runHelper(HelperAction action, QString *error = nullptr);
    static bool startUserService(QString *error = nullptr);
    static bool stopUserService(QString *error = nullptr);
};

} // namespace speecher
