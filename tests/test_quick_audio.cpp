#include "common/test_doubles.h"
#include "common/test_suites.h"
#include "common/test_system_singletons.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QPointer>
#include <QQmlEngine>
#include <QSignalSpy>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;
using speecher::test::FakeAudioInput;

namespace {

const QString yeti = QStringLiteral("alsa_input.usb-Blue_Yeti");
const QString builtIn = QStringLiteral("alsa_input.pci-analog-stereo");

// Audio over a made-up set of microphones, with the test's inputs fakes
// that the test drives by hand.
struct Rig {
    explicit Rig(ApplicationController &controller)
        : audio(&controller, nullptr, [this] { return microphones; }, [this](QObject *parent) {
              auto *input = new FakeAudioInput(parent);
              input->startResult = startResult;
              input->startError = startError;
              input->onStart = onStart;
              inputs.append(input);
              return input;
          })
    {
    }

    FakeAudioInput *input() const { return inputs.isEmpty() ? nullptr : inputs.last().data(); }

    QList<AudioInputDeviceInfo> microphones{{yeti, QStringLiteral("Blue Yeti"), false},
                                            {builtIn, QStringLiteral("Built-in microphone"), true}};
    bool startResult = true;
    QString startError;
    // Runs inside each input's start(), while the device opens.
    std::function<void()> onStart;
    QList<QPointer<FakeAudioInput>> inputs;
    QuickAudio audio;
    // What holds the test: the page showing it.
    QObject page;
};

QVariantMap device(const QString &id, const QString &label, bool isDefault)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("isDefault"), isDefault}};
}

} // namespace

// The UI's Audio: the platform's microphones and the microphone test, and
// Microphones, which words them for the microphone page and setup.
class QuickAudioTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void devicesFollowThePlatform()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        QSignalSpy changed(&rig.audio, &QuickAudio::devicesChanged);

        QCOMPARE(rig.audio.devices(), (QVariantList{device(yeti, QStringLiteral("Blue Yeti"), false),
                                                    device(builtIn, QStringLiteral("Built-in microphone"), true)}));
        QVERIFY(rig.audio.hasMicrophones());

        // Asked again with nothing new, nothing moves.
        rig.audio.refreshDevices();
        QCOMPARE(changed.count(), 0);

        // Unplugged.
        rig.microphones.clear();
        rig.audio.refreshDevices();
        QCOMPARE(changed.count(), 1);
        QVERIFY(!rig.audio.hasMicrophones());
    }

    void theTestListensToTheSavedDevice()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("audioDevice"), yeti);
        Rig rig(controller);

        rig.audio.holdTest(&rig.page);
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.input()->device, yeti);

        emit rig.input()->levelChanged(0.4f);
        QCOMPARE(rig.audio.level(), qreal(0.4f));

        rig.audio.releaseTest(&rig.page);
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.audio.level(), qreal(0));
    }

    void heardIsInputSinceTheTestStarted()
    {
        ApplicationController controller(true);
        Rig rig(controller);

        rig.audio.holdTest(&rig.page);
        // A dead microphone's hiss is not input.
        emit rig.input()->levelChanged(0.005f);
        QVERIFY(!rig.audio.heard());
        emit rig.input()->levelChanged(0.2f);
        QVERIFY(rig.audio.heard());

        // Stopping keeps it, for setup's check; starting again asks afresh.
        rig.audio.releaseTest(&rig.page);
        QVERIFY(rig.audio.heard());
        rig.audio.holdTest(&rig.page);
        QVERIFY(!rig.audio.heard());
    }

    void choosingAnotherDeviceMovesTheTest()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.holdTest(&rig.page);
        emit rig.input()->levelChanged(0.2f);
        QVERIFY(rig.audio.heard());

        QuickSettings settings(&controller);
        settings.set(QStringLiteral("audioDevice"), yeti);
        QCOMPARE(SettingsStore().audioInputDeviceId(), yeti);
        QVERIFY(!rig.audio.heard());
        // Not inside the settings store's signal: opening a device can wait.
        QCOMPARE(rig.inputs.size(), 1);

        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.inputs.size(), 2);
        QCOMPARE(rig.input()->device, yeti);
        QVERIFY(!rig.audio.heard());
    }

    void aDeviceThatWillNotOpenSaysWhy()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.startResult = false;
        rig.startError = QStringLiteral("Microphone \"Blue Yeti\" is in use by another app.");

        rig.audio.holdTest(&rig.page);
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.audio.testError(), rig.startError);
        // Not tried again and again, nor for a setting that isn't the device.
        QCoreApplication::processEvents();
        AudioCaptureSettings capture = controller.settings()->audioCaptureSettings();
        capture.preRollMs += 100;
        controller.settings()->setAudioCaptureSettings(capture);
        QCoreApplication::processEvents();
        QCOMPARE(rig.inputs.size(), 1);

        // A microphone plugged in tries again, once.
        rig.microphones.append({QStringLiteral("usb-rode"), QStringLiteral("Rode NT-USB"), false});
        rig.audio.refreshDevices();
        QCoreApplication::processEvents();
        QCOMPARE(rig.inputs.size(), 2);
        QCoreApplication::processEvents();
        QCOMPARE(rig.inputs.size(), 2);

        // So does holding it again, as Start test does.
        rig.startResult = true;
        rig.audio.holdTest(&rig.page);
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.audio.testError(), QString());
    }

    void aDeviceThatFailsWhileRunningStopsTheTest()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.holdTest(&rig.page);
        emit rig.input()->levelChanged(0.2f);

        rig.input()->emitFailure(QStringLiteral("Microphone \"Blue Yeti\" was disconnected."));
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.audio.testError(), QStringLiteral("Microphone \"Blue Yeti\" was disconnected."));
        // What it heard is gone with it, so setup asks again.
        QVERIFY(!rig.audio.heard());

        // It tries again, on whatever records now.
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.inputs.size(), 2);

        // Once: failing again, it waits rather than reopening the device.
        rig.input()->emitFailure(QStringLiteral("Microphone \"Blue Yeti\" was disconnected."));
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.inputs.size(), 2);
    }

    // Choosing another microphone while the first still opens, and the first
    // then failing: the failure is the first's, so the second is tried.
    void aFailureCountsAgainstTheDeviceThatFailed()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("audioDevice"), yeti);
        Rig rig(controller);
        bool once = true;
        rig.onStart = [&] {
            if (std::exchange(once, false)) {
                QuickSettings(&controller).set(QStringLiteral("audioDevice"), builtIn);
                rig.input()->startResult = false;
            }
        };

        rig.audio.holdTest(&rig.page);
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.input()->device, builtIn);

        // The first, chosen again during a dictation, is tried afterwards:
        // its failure came after the choice had moved on, so holds nothing.
        rig.input()->startResult = true;
        emit controller.stateChanged(DictationState::Listening);
        QuickSettings(&controller).set(QStringLiteral("audioDevice"), yeti);
        emit controller.stateChanged(DictationState::Idle);
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.input()->device, yeti);
    }

    // A microphone that would not open is tried again once chosen again.
    void choosingAFailedDeviceAgainTriesIt()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("audioDevice"), yeti);
        Rig rig(controller);
        QuickSettings settings(&controller);
        rig.startResult = false;
        rig.audio.holdTest(&rig.page);
        rig.startResult = true;
        settings.set(QStringLiteral("audioDevice"), builtIn);
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));

        // Chosen during a dictation, so the test starts on it afterwards.
        emit controller.stateChanged(DictationState::Listening);
        settings.set(QStringLiteral("audioDevice"), yeti);
        emit controller.stateChanged(DictationState::Idle);
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.input()->device, yeti);
    }

    void dictationTakesTheMicrophone()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.holdTest(&rig.page);

        emit controller.stateChanged(DictationState::Listening);
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QVERIFY(!rig.audio.canToggleTest());
        rig.audio.holdTest(&rig.page);
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));

        // The page still shows it, so it listens again.
        emit controller.stateChanged(DictationState::Idle);
        QVERIFY(rig.audio.canToggleTest());
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
    }

    // Setup's step holds the test while Main's microphone page holds and
    // releases it: nothing Main does stops it.
    void setupKeepsItsTestWhateverMainDoes()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        QObject micCheck;
        rig.audio.holdTest(&rig.page);

        rig.audio.holdTest(&micCheck);
        rig.audio.releaseTest(&micCheck);
        QVERIFY(!rig.audio.testHolders().contains(&micCheck));
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.inputs.size(), 1);

        // Once nothing holds it, it stops; a holder going counts.
        {
            QObject other;
            rig.audio.holdTest(&other);
            rig.audio.releaseTest(&rig.page);
            QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        }
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
    }

    // Leaving setup's step and coming back while the device still opens.
    void aQuickLeaveAndReturnKeepsTheTest()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        QObject returned;
        bool once = true;
        rig.onStart = [&] {
            if (std::exchange(once, false)) {
                rig.audio.releaseTest(&rig.page);
                rig.audio.holdTest(&returned);
            }
        };

        rig.audio.holdTest(&rig.page);
        QCoreApplication::processEvents();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.audio.testHolders(), QObjectList{&returned});
    }

    // What was heard on one microphone does not vouch for another.
    void savingAnotherDeviceForgetsWhatWasHeard()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.holdTest(&rig.page);
        emit rig.input()->levelChanged(0.2f);
        rig.audio.releaseTest(&rig.page);
        QVERIFY(rig.audio.heard());
        QSignalSpy changed(&rig.audio, &QuickAudio::testChanged);

        QuickSettings(&controller).set(QStringLiteral("audioDevice"), yeti);
        QVERIFY(!rig.audio.heard());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
    }

    // The device choices as the page and setup offer them.
    void microphonesWordsTheChoices()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        // What the test says reads the microphone access check.
        test::SystemSingletons system(&controller);
        Rig rig(controller);
        QQmlEngine engine;
        QObject *microphones = engine.singletonInstance<QObject *>("Speecher.App", "Microphones");
        QVERIFY(microphones);
        const auto options = [&] {
            QStringList described;
            for (const QVariant &option : microphones->property("deviceOptions").toList()) {
                const QVariantMap map = option.toMap();
                described.append(map.value(QStringLiteral("id")).toString() + QLatin1Char('=')
                                 + map.value(QStringLiteral("label")).toString() + QLatin1Char('/')
                                 + map.value(QStringLiteral("help")).toString());
            }
            return described;
        };

        // The system default names the platform's default, not the first.
        QCOMPARE(options(), (QStringList{QStringLiteral("=System default/Built-in microphone right now"),
                                         yeti + QStringLiteral("=Blue Yeti/"),
                                         builtIn + QStringLiteral("=Built-in microphone/")}));
        QCOMPARE(microphones->property("deviceName").toString(), QStringLiteral("Built-in microphone"));

        // A saved microphone that has gone stays, marked missing.
        settings.set(QStringLiteral("audioDevice"), QStringLiteral("gone"));
        QVERIFY(microphones->property("deviceMissing").toBool());
        QCOMPARE(options().last(),
                 QStringLiteral("gone=Missing microphone/This saved microphone is not currently available."));

        rig.microphones.clear();
        rig.audio.refreshDevices();
        QCOMPARE(options(), (QStringList{QStringLiteral("=No microphone found/Connect or enable an input device, then try again."),
                                         QStringLiteral("gone=Missing microphone/This saved microphone is not currently available.")}));
    }
};

int runQuickAudioTests(int argc, char **argv)
{
    QuickAudioTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_audio.moc"
