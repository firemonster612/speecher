#include "common/test_doubles.h"
#include "common/test_suites.h"

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
    QList<QPointer<FakeAudioInput>> inputs;
    QuickAudio audio;
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
        controller.settings()->setAudioInputDeviceId(yeti);
        Rig rig(controller);

        rig.audio.toggleTest();
        QCOMPARE(rig.audio.testState(), QStringLiteral("running"));
        QCOMPARE(rig.input()->device, yeti);

        emit rig.input()->levelChanged(0.4f);
        QCOMPARE(rig.audio.level(), qreal(0.4f));

        rig.audio.toggleTest();
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.audio.level(), qreal(0));
    }

    void heardIsInputSinceTheTestStarted()
    {
        ApplicationController controller(true);
        Rig rig(controller);

        rig.audio.toggleTest();
        // A dead microphone's hiss is not input.
        emit rig.input()->levelChanged(0.005f);
        QVERIFY(!rig.audio.heard());
        emit rig.input()->levelChanged(0.2f);
        QVERIFY(rig.audio.heard());

        // Stopping keeps it, for setup's check; starting again asks afresh.
        rig.audio.stopTest();
        QVERIFY(rig.audio.heard());
        rig.audio.toggleTest();
        QVERIFY(!rig.audio.heard());
    }

    void choosingAnotherDeviceMovesTheTest()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.toggleTest();
        emit rig.input()->levelChanged(0.2f);
        QVERIFY(rig.audio.heard());

        QuickSettings settings(&controller);
        settings.set(QStringLiteral("audioDevice"), yeti);

        QCOMPARE(SettingsStore().audioInputDeviceId(), yeti);
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

        rig.audio.toggleTest();
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.audio.testError(), rig.startError);

        rig.startResult = true;
        rig.audio.toggleTest();
        QCOMPARE(rig.audio.testError(), QString());
    }

    void aDeviceThatFailsWhileRunningStopsTheTest()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.toggleTest();
        emit rig.input()->levelChanged(0.2f);

        rig.input()->emitFailure(QStringLiteral("Microphone \"Blue Yeti\" was disconnected."));
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QCOMPARE(rig.audio.testError(), QStringLiteral("Microphone \"Blue Yeti\" was disconnected."));
        // What it heard is gone with it, so setup asks again.
        QVERIFY(!rig.audio.heard());
    }

    void dictationTakesTheMicrophone()
    {
        ApplicationController controller(true);
        Rig rig(controller);
        rig.audio.toggleTest();

        emit controller.stateChanged(QStringLiteral("listening"));
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));
        QVERIFY(!rig.audio.canToggleTest());
        rig.audio.toggleTest();
        QCOMPARE(rig.audio.testState(), QStringLiteral("stopped"));

        emit controller.stateChanged(QStringLiteral("idle"));
        QVERIFY(rig.audio.canToggleTest());
    }

    // The device choices as the page and setup offer them.
    void microphonesWordsTheChoices()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
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
        QCOMPARE(options(), (QStringList{QStringLiteral("=No microphones found/Connect or enable an input device, then try again."),
                                         QStringLiteral("gone=Missing microphone/This saved microphone is not currently available.")}));
    }
};

int runQuickAudioTests(int argc, char **argv)
{
    QuickAudioTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_audio.moc"
