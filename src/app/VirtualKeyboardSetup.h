#pragma once

#include "output/YdotoolSetup.h"

#include <QObject>
#include <QTimer>

#include <functional>

namespace speecher {

class SettingsStore;

// The virtual keyboard (ydotool) Speecher pastes with, set up, tested, started,
// turned off and removed for whichever window asks. The privileged steps run
// the setup helper through pkexec, whose own prompt asks for the password;
// they and the typing run off the calling thread, one operation at a time.
// Speecher starts pasting with the keyboard only after a typing test has put
// the test text into the window's own field.
class VirtualKeyboardSetup final : public QObject {
    Q_OBJECT

public:
    enum class Operation {
        None,
        SettingUp,
        Starting,
        Removing,
    };

    // Where the typing test stands: Ready waits for the window's field to
    // take focus and run it, Missed is a run whose text never arrived.
    enum class Test {
        Off,
        Ready,
        Typing,
        Missed,
    };

    // The step that went wrong, for the window to word.
    enum class Failure {
        SetUp,
        Start,
        Type,
        Stop,
        Remove,
        // The helper finished, but some of the files it set up are still there.
        FilesLeft,
    };

    // What the output method becomes once setUp has installed the keyboard.
    // The setup assistant pastes Automatically: insert where it can, then the
    // virtual keyboard.
    enum class AfterSetUp {
        KeepOutputMethod,
        PasteAutomatically,
    };

    static bool typeWithYdotool(const QString &text, QString *error);

    // What the setup touches outside the process, so tests can stand in for
    // ydotool, pkexec and systemd. Each runs off the calling thread except
    // probe.
    struct System {
        std::function<YdotoolSetupStatus(bool enabledInSpeecher)> probe = YdotoolSetup::probe;
        std::function<bool(YdotoolSetup::HelperAction, QString *error)> runHelper = YdotoolSetup::runHelper;
        std::function<bool(QString *error)> startService = YdotoolSetup::startUserService;
        std::function<bool(QString *error)> stopService = YdotoolSetup::stopUserService;
        std::function<bool(const QString &text, QString *error)> type = typeWithYdotool;
    };

    // How long typed text may take to reach the field before the test fails.
    static constexpr int testArrivalMs = 1500;

    explicit VirtualKeyboardSetup(SettingsStore *settings, QObject *parent = nullptr);
    VirtualKeyboardSetup(SettingsStore *settings, System system, QObject *parent = nullptr);
    ~VirtualKeyboardSetup() override;

    // Probed again by refresh and after every operation.
    YdotoolSetupStatus status() const { return m_status; }
    Operation operation() const { return m_operation; }
    Test test() const { return m_test; }
    // The text runTest types, which the field must receive.
    static QString testText();

    void refresh();
    // Installs the setup through the helper and starts its service. A setup
    // that comes up ready goes on to the typing test.
    void setUp(AfterSetUp after = AfterSetUp::KeepOutputMethod);
    void startService();
    // Opens the typing test for a keyboard that is ready but off in Speecher.
    void beginTest();
    // Types testText into whatever has focus, which the caller has just
    // focused. receivedTestText then reports what arrived.
    void runTest();
    // What the test field holds. The test text turns the keyboard on in
    // Speecher; anything else waits until testArrivalMs runs out.
    void receivedTestText(const QString &text);
    void cancelTest();
    // Stops pasting with the keyboard and keeps its setup for later.
    void disable();
    // Stops the service and removes the setup through the helper.
    void remove();

signals:
    void changed();
    // An operation went wrong at the step failure names. error is why, in
    // the words of the part that failed (the helper, systemd, ydotool), or
    // empty where it said nothing.
    void failed(VirtualKeyboardSetup::Failure failure, const QString &error);

private:
    // Runs work off this thread; done then runs here, unless this went first.
    void runOffThread(Operation operation, std::function<void()> work, std::function<void()> done);
    void setTest(Test test);

    SettingsStore *m_settings;
    System m_system;
    YdotoolSetupStatus m_status;
    Operation m_operation = Operation::None;
    Test m_test = Test::Off;
    QTimer m_arrival;
};

} // namespace speecher
