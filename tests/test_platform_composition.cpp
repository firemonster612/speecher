#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/CommandLine.h"
#include "app/PlatformComposition.h"
#include "app/ShortcutSuspendingDelivery.h"
#include "core/LearnedCorrection.h"
#include "core/SettingsStore.h"
#include "transcribe/FileTranscriptionSession.h"
#include "dictation/DictationSession.h"
#include "platform/CorrectionDiff.h"
#include "platform/mac/MacMediaController.h"
#include "app/LocalSetup.h"
#include "providers/LocalModelStore.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/SingleKeyShortcutBinder.h"
#ifdef Q_OS_LINUX
#include "platform/KGlobalAccelShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"
#include "platform/PortalGlobalShortcutBinder.h"
#include "transcribe/FileTranscriptionSession.h"
#include "ui/SetupAssistant.h"
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#include "ui/setup/SetupPages.h"
#endif

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QPalette>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSettings>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QtEndian>

#ifdef SPEECHER_WITH_KASSISTANT
#include <KPageWidget>
#endif
#include <QTest>

#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

using namespace speecher;

namespace {

class FakeGlobalShortcutBinder final : public GlobalShortcutBinder {
public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    bool supported() const override
    {
        return shortcutsSupported;
    }

    bool supportKnown() const override { return shortcutSupportKnown; }

    bool usesDesktopShortcutChooser() const override { return desktopChooser; }

    QString unsupportedReason() const override
    {
        return unsupported;
    }

    void bind() override
    {
        bindCount += 1;
    }

    ShortcutBinding shortcut() const override
    {
        return m_shortcut;
    }

    bool setShortcut(const ShortcutBinding &shortcut, QString *error) override
    {
        if (!setShortcutError.isEmpty()) {
            if (error) {
                *error = setShortcutError;
            }
            return false;
        }
        if (shortcut.isEmpty()) {
            if (error) {
                *error = QStringLiteral("fake binder rejects an empty sequence");
            }
            return false;
        }
        m_shortcut = shortcut;
        return true;
    }

    void registerShortcut() override
    {
        registerCount += 1;
    }

    bool removeRegistration(QString *) override
    {
        publishShortcut({});
        return true;
    }

    void setArmed(bool value) override { armed = value; }

    void suspend() override { suspendCount += 1; }

    QString resume() override
    {
        resumeCount += 1;
        return resumeError;
    }

    void publishShortcut(const ShortcutBinding &shortcut)
    {
        m_shortcut = shortcut;
        emit bindingChanged();
    }

    void publishRegistrationResult(bool bound, const QString &detail)
    {
        emit registrationFinished(bound, detail);
    }

    void publishSupport(bool known, bool supported)
    {
        shortcutSupportKnown = known;
        shortcutsSupported = supported;
        emit supportChanged();
    }

    int bindCount = 0;
    bool armed = false;
    int registerCount = 0;
    int suspendCount = 0;
    int resumeCount = 0;
    bool shortcutSupportKnown = true;
    bool shortcutsSupported = true;
    bool desktopChooser = false;
    QString unsupported;
    QString setShortcutError;
    QString resumeError;

private:
    ShortcutBinding m_shortcut;
};

// Drives SingleKeyShortcutBinder's down/up bookkeeping directly, the way a
// platform subclass does from its key observer.
class FakeSingleKeyShortcutBinder final : public SingleKeyShortcutBinder {
public:
    using SingleKeyShortcutBinder::SingleKeyShortcutBinder;
    using SingleKeyShortcutBinder::keyDown;
    using SingleKeyShortcutBinder::keyUp;

    bool supported() const override { return true; }

protected:
    QString watch(const PhysicalKey &) override { return {}; }
    void unwatch() override {}
};

// Answers for itself everything the seam added, and delegates the ports it does
// not care about to the composition the platform already provides.
class FakePlatformComposition final : public PlatformComposition {
public:
    explicit FakePlatformComposition(std::shared_ptr<const PlatformComposition> delegate)
        : m_delegate(std::move(delegate))
    {
    }

    QString outputSummary() const override
    {
        return QStringLiteral("Fake: nothing is delivered");
    }

    QString ipcListenName() const override
    {
        return m_delegate->ipcListenName();
    }

    QStringList ipcConnectCandidates() const override
    {
        return m_delegate->ipcConnectCandidates();
    }

    QString detachedExecutablePath() const override
    {
        return m_delegate->detachedExecutablePath();
    }

    QList<AudioInputDeviceInfo> availableAudioInputDevices() const override
    {
        return {{QStringLiteral("fake-device"), QStringLiteral("Fake microphone")}};
    }

    AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const override
    {
        return m_delegate->createAudioInput(settings, parent);
    }

    void requestMicrophoneAccess(QObject *, std::function<void(bool)> completed) const override
    {
        microphoneAnswer = std::move(completed);
    }

    MediaController *createMediaController(QObject *parent) const override
    {
        return m_delegate->createMediaController(parent);
    }

    TargetProvider *createTargetProvider(QObject *parent) const override
    {
        return m_delegate->createTargetProvider(parent);
    }

    ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const override
    {
        return m_delegate->createScreenshotContextProvider(parent);
    }

    TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const override
    {
        return m_delegate->createTextDelivery(targetProvider, parent);
    }

    PopupPositioner *createPopupPositioner(QObject *parent) const override
    {
        return m_delegate->createPopupPositioner(parent);
    }

    GlobalShortcutBinder *createGlobalShortcutBinder(GlobalShortcutRole role, QObject *parent) const override
    {
        auto *created = new FakeGlobalShortcutBinder(parent);
        (role == GlobalShortcutRole::Cancel ? cancelBinder
         : role == GlobalShortcutRole::Pause ? pauseBinder
                                              : binder) = created;
        return created;
    }

    AccessibilityState accessibilityState() const override
    {
        return accessibility;
    }

    void watchAccessibilityChanges(QObject *, std::function<void()> refresh) const override
    {
        accessibilityRefresh = std::move(refresh);
    }

    bool requestAccessibility(QString *) const override
    {
        return true;
    }

    bool enableAccessibilityPermanently(QString *) const override
    {
        return true;
    }

    // Empty accepts the change, as every computer that has a login item does;
    // anything else is what that computer refused with.
    bool setLaunchAtLogin(bool, QString *error) const override
    {
        if (launchAtLoginError.isEmpty()) {
            return true;
        }
        if (error) {
            *error = launchAtLoginError;
        }
        return false;
    }

    mutable QString launchAtLoginError;

    mutable std::function<void(bool)> microphoneAnswer;
    mutable FakeGlobalShortcutBinder *binder = nullptr;
    mutable FakeGlobalShortcutBinder *cancelBinder = nullptr;
    mutable FakeGlobalShortcutBinder *pauseBinder = nullptr;
    mutable AccessibilityState accessibility{true, true, false};
    mutable std::function<void()> accessibilityRefresh;

private:
    std::shared_ptr<const PlatformComposition> m_delegate;
};

// Records what the controller asks of a user interface, so the seam can be
// checked without a window on screen.
class FakeAppFrontEnd final : public AppFrontEnd {
public:
    void showMainWindow() override
    {
        calls << QStringLiteral("showMainWindow");
    }

    void hideMainWindow() override
    {
        calls << QStringLiteral("hideMainWindow");
    }

    void showSettingsWindow() override
    {
        calls << QStringLiteral("showSettingsWindow");
    }

    void showSetupAssistant(SetupAssistantPage page) override
    {
        calls << (page == SetupAssistantPage::All
                      ? QStringLiteral("showSetupAssistant")
                      : QStringLiteral("showSetupAssistant GlobalShortcut"));
    }

    void showTranscribeFiles(const QStringList &paths) override
    {
        calls << QStringLiteral("showTranscribeFiles ") + paths.join(QLatin1Char(' '));
    }

    bool captureMainWindow(const QString &path) override
    {
        calls << QStringLiteral("captureMainWindow ") + path;
        return true;
    }

    void showDictationError(const QString &message, const PopupErrorAction &) override
    {
        calls << QStringLiteral("showDictationError ") + message;
    }

    void alert() override
    {
        calls << QStringLiteral("alert");
    }

    void notifyIfNoWindowShown(const QString &title, const QString &message, const QString &) override
    {
        calls << QStringLiteral("notify %1: %2").arg(title, message);
    }

    QStringList calls;
};

} // namespace

class PlatformCompositionTests : public QObject {
    Q_OBJECT

private slots:
    void shortcutReleaseWhilePermissionPending_data()
    {
        QTest::addColumn<bool>("hold");
        QTest::addColumn<bool>("grantBeforeRelease");
        QTest::newRow("tap before grant") << false << false;
        QTest::newRow("hold before grant") << true << false;
        QTest::newRow("tap after grant") << false << true;
        QTest::newRow("hold after grant") << true << true;
    }

    void shortcutReleaseWhilePermissionPending()
    {
        QFETCH(bool, hold);
        QFETCH(bool, grantBeforeRelease);
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        QCOMPARE(controller.session()->state(), DictationState::Idle);
        if (grantBeforeRelease) platform->microphoneAnswer(true);
        if (hold) QTest::qSleep(410);
        emit platform->binder->deactivated();
        if (!grantBeforeRelease) platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), hold ? DictationState::Idle : DictationState::Starting);
        controller.stopListening();
    }

    void dictationAndFileTranscriptionExcludeEachOther()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("silence.wav"));
        {
            // One second of 16 kHz mono s16 silence.
            const QByteArray data(32000, '\0');
            const auto le32 = [](quint32 v) { QByteArray b(4, 0); qToLittleEndian(v, b.data()); return b; };
            const auto le16 = [](quint16 v) { QByteArray b(2, 0); qToLittleEndian(v, b.data()); return b; };
            QFile file(audio);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF" + le32(36 + data.size()) + "WAVEfmt " + le32(16) + le16(1) + le16(1)
                       + le32(16000) + le32(32000) + le16(2) + le16(16) + "data" + le32(data.size()) + data);
        }
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");

        // A batch in flight turns the dictation shortcut away.
        QVERIFY(controller.startFileTranscription({audio}, options));
        controller.toggle();
        QVERIFY(!platform->microphoneAnswer);
        controller.fileTranscription()->cancel();

        // A dictation waiting on the microphone turns a batch away.
        controller.toggle();
        QVERIFY(platform->microphoneAnswer);
        QString error;
        QVERIFY(!controller.startFileTranscription({audio}, options, &error));
        QVERIFY(!error.isEmpty());
        controller.stopListening();
    }

    void shortcutReleasePreservesRecordingError()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        auto *audio = controller.findChild<AudioInput *>();
        QVERIFY(audio);
        emit audio->failed(QStringLiteral("Test microphone disconnected"));
        QCOMPARE(controller.session()->state(), DictationState::Error);
        QSignalSpy hidden(controller.session(), &DictationSession::popupHideRequested);
        QTest::qSleep(410);
        emit platform->binder->deactivated();
        QCOMPARE(controller.session()->state(), DictationState::Error);
        QVERIFY(hidden.isEmpty());
    }

    void hybridTimesTheHoldByTheKeyNotByWhenTheReleaseIsHandled_data()
    {
        QTest::addColumn<qint64>("heldMs");
        QTest::addColumn<bool>("keepsDictating");
        QTest::newRow("tap") << qint64(80) << true;
        QTest::newRow("hold") << qint64(410) << false;
    }

    // The first dictation after a launch opens the microphone cold, and the
    // main thread only gets to a tap's release once that is done. The binder's
    // physical hold, not the delay, decides whether it was push-to-talk.
    void hybridTimesTheHoldByTheKeyNotByWhenTheReleaseIsHandled()
    {
        QFETCH(qint64, heldMs);
        QFETCH(bool, keepsDictating);
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const ShortcutActivationMode mode = controller.settings()->shortcutActivationMode();
        const auto restore = qScopeGuard([&] {
            controller.settings()->setSetupCompleted(setupCompleted);
            controller.settings()->setShortcutActivationMode(mode);
        });
        controller.settings()->setSetupCompleted(true);
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::Hybrid);

        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        QTest::qSleep(410);
        emit platform->binder->deactivated(heldMs);
        QCOMPARE(controller.session()->state(),
                 keepsDictating ? DictationState::Starting : DictationState::Idle);
        controller.stopListening();
    }

    // A single-key backend that timestamps its events reports the hold between
    // them; one that cannot leaves the controller to time it.
    void singleKeyBinderReportsThePhysicalHold()
    {
        FakeSingleKeyShortcutBinder binder;
        QSignalSpy released(&binder, &GlobalShortcutBinder::deactivated);

        binder.keyDown(1000);
        binder.keyUp(1080);
        QCOMPARE(released.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), 80);

        binder.keyDown();
        binder.keyUp();
        QCOMPARE(released.count(), 1);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), -1);

        // A press without a timestamp leaves nothing to measure a timed
        // release against.
        binder.keyDown();
        binder.keyUp(2000);
        QCOMPARE(released.takeFirst().at(0).toLongLong(), -1);
    }

    // A held key's auto-repeat presses arrive as extra activated() signals on
    // some Linux desktops. Once the platform has proven it reports releases,
    // repeats between a press and its release must not toggle the session off.
    void shortcutAutoRepeatDoesNotToggle()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);

        // A completed tap teaches the controller that releases arrive here.
        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        emit platform->binder->deactivated();
        controller.stopListening();
        QCOMPARE(controller.session()->state(), DictationState::Idle);

        platform->microphoneAnswer = nullptr;
        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        emit platform->binder->activated();
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        QTest::qSleep(410);
        emit platform->binder->deactivated();
        QCOMPARE(controller.session()->state(), DictationState::Idle);
    }

    // Before any release is seen the guard is intentionally inert, so this same
    // input (two presses, no release) also stands in for the first-ever hold on
    // a backend that streams auto-repeat: it toggles off, the irreducible cost
    // of the toggle bias. Every hold after the first release is covered by
    // shortcutAutoRepeatDoesNotToggle. The assertion here guards the
    // m_shortcutReleaseSeen condition against being broadened away: a bare
    // m_shortcutDown guard would swallow this press forever on a no-release
    // desktop, stranding the session.
    void secondPressWithoutReleaseStillToggles()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        emit platform->binder->activated();
        QCOMPARE(controller.session()->state(), DictationState::Idle);
    }

    // Push-to-talk starts nothing for a brush of the key, and a real hold
    // dictates until the key comes up.
    void pushToTalkIgnoresABrushAndEndsWithTheKey()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const ShortcutActivationMode mode = controller.settings()->shortcutActivationMode();
        const auto restore = qScopeGuard([&] {
            controller.settings()->setSetupCompleted(setupCompleted);
            controller.settings()->setShortcutActivationMode(mode);
        });
        controller.settings()->setSetupCompleted(true);
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::PushToTalk);

        emit platform->binder->activated();
        QTest::qSleep(50);
        emit platform->binder->deactivated();
        QTest::qWait(300);
        QVERIFY(!platform->microphoneAnswer);
        QCOMPARE(controller.session()->state(), DictationState::Idle);

        emit platform->binder->activated();
        QTRY_VERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        emit platform->binder->deactivated();
        QCOMPARE(controller.session()->state(), DictationState::Idle);
    }

    void toggleModeIgnoresReleaseAndTogglesOnEveryPress()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const ShortcutActivationMode mode = controller.settings()->shortcutActivationMode();
        const auto restore = qScopeGuard([&] {
            controller.settings()->setSetupCompleted(setupCompleted);
            controller.settings()->setShortcutActivationMode(mode);
        });
        controller.settings()->setSetupCompleted(true);
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::Toggle);

        emit platform->binder->activated();
        QVERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        // A hold that hybrid would treat as push-to-talk changes nothing here.
        QTest::qSleep(300);
        emit platform->binder->deactivated();
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        emit platform->binder->activated();
        QCOMPARE(controller.session()->state(), DictationState::Idle);
    }

    void stopCancelsPendingMicrophoneStart()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        controller.startListening();
        QVERIFY(platform->microphoneAnswer);
        controller.stopListening();
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Idle);
    }

    // The Cancel Shortcut's press throws away a start still waiting on the
    // microphone grant.
    void cancelShortcutDropsAPendingMicrophoneStart()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        controller.startListening();
        QVERIFY(platform->microphoneAnswer);
        emit platform->cancelBinder->activated();
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Idle);
    }

    // Cancel drops push-to-talk's deferred start, and the key's release after
    // a cancel leaves the next dictation alone.
    void cancelDropsADeferredPushToTalkStartAndItsRelease()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const ShortcutActivationMode mode = controller.settings()->shortcutActivationMode();
        const auto restore = qScopeGuard([&] {
            controller.settings()->setSetupCompleted(setupCompleted);
            controller.settings()->setShortcutActivationMode(mode);
        });
        controller.settings()->setSetupCompleted(true);
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::PushToTalk);

        emit platform->binder->activated();
        controller.cancel();
        QTest::qWait(300);
        QVERIFY(!platform->microphoneAnswer);
        emit platform->binder->deactivated();

        emit platform->binder->activated();
        QTRY_VERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        controller.cancel();
        QCOMPARE(controller.session()->state(), DictationState::Idle);
        platform->microphoneAnswer = nullptr;
        controller.startListening();
        platform->microphoneAnswer(true);
        emit platform->binder->deactivated();
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        controller.cancel();
    }

    // No two Global Shortcuts share a binding.
    void cancelShortcutRefusesTheDictationShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const ShortcutBinding keys(QKeySequence(Qt::META | Qt::ALT | Qt::Key_C));
        QVERIFY(controller.setGlobalShortcut(keys));
        QString error;
        QVERIFY(!controller.setGlobalShortcut(keys, &error, GlobalShortcutRole::Cancel));
        QCOMPARE(error, QStringLiteral("That is already the Global Shortcut for dictation."));
        QVERIFY(controller.globalShortcut(GlobalShortcutRole::Cancel).isEmpty());

        const ShortcutBinding c{QKeySequence(Qt::Key_C)};
        QVERIFY(controller.setGlobalShortcut(c, nullptr, GlobalShortcutRole::Cancel));
        QVERIFY(!controller.setGlobalShortcut(c, &error, GlobalShortcutRole::Pause));
        QCOMPARE(error, QStringLiteral("That is already the Cancel Shortcut."));
        QVERIFY(controller.globalShortcut(GlobalShortcutRole::Pause).isEmpty());
    }

    // The Cancel and Pause Shortcuts hold their keys only while a Dictation
    // Session can use them, so a bare key types normally the rest of the time.
    void sessionShortcutsAreArmedOnlyDuringASession()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->setupCompleted();
        const auto restore = qScopeGuard([&] { controller.settings()->setSetupCompleted(setupCompleted); });
        controller.settings()->setSetupCompleted(true);
        QVERIFY(!platform->cancelBinder->armed);
        QVERIFY(!platform->pauseBinder->armed);

        controller.startListening();
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);
        QVERIFY(platform->cancelBinder->armed);
        // Pause applies from Listening on.
        QVERIFY(!platform->pauseBinder->armed);

        controller.cancel();
        QCOMPARE(controller.session()->state(), DictationState::Idle);
        QVERIFY(!platform->cancelBinder->armed);
        QVERIFY(!platform->pauseBinder->armed);
    }

    // A Cancel Shortcut that could not be taken back after recording is
    // reported as the Cancel Shortcut's, so its row shows it. The dictation
    // shortcut's error wins when both fail.
    void resumingTheShortcutsReportsWhichShortcutFailed()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        GlobalShortcutRole failed = GlobalShortcutRole::Dictation;
        platform->cancelBinder->resumeError = QStringLiteral("cancel taken");
        controller.suspendGlobalShortcut();
        QCOMPARE(controller.resumeGlobalShortcut(&failed), QStringLiteral("cancel taken"));
        QCOMPARE(int(failed), int(GlobalShortcutRole::Cancel));

        platform->binder->resumeError = QStringLiteral("dictation taken");
        controller.suspendGlobalShortcut();
        QCOMPARE(controller.resumeGlobalShortcut(&failed), QStringLiteral("dictation taken"));
        QCOMPARE(int(failed), int(GlobalShortcutRole::Dictation));

        platform->binder->resumeError.clear();
        platform->cancelBinder->resumeError.clear();
        platform->pauseBinder->resumeError = QStringLiteral("pause taken");
        controller.suspendGlobalShortcut();
        QCOMPARE(controller.resumeGlobalShortcut(&failed), QStringLiteral("pause taken"));
        QCOMPARE(int(failed), int(GlobalShortcutRole::Pause));
    }

    // A watcher cannot stop a key, so it refuses a session shortcut on a key
    // that types; the dictation shortcut may still take one.
    void watchersRefuseTypingKeysForSessionShortcuts()
    {
        const ShortcutBinding c = ShortcutBinding::singleKey(QStringLiteral("KeyC"));
        const ShortcutBinding f13 = ShortcutBinding::singleKey(QStringLiteral("F13"));
        for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
            FakeSingleKeyShortcutBinder session(GlobalShortcutBinder::actionFor(role));
            QCOMPARE(session.unsupportedBindingReason(c), watchedKeyStillTypesText(c));
            QVERIFY(session.unsupportedBindingReason(f13).isEmpty());
        }
        FakeSingleKeyShortcutBinder dictation(GlobalShortcutBinder::actionFor(GlobalShortcutRole::Dictation));
        QVERIFY(dictation.unsupportedBindingReason(c).isEmpty());
    }

    // The Cancel Shortcut keeps its binding under its own key, next to the
    // dictation shortcut's, and a new binder reads it back.
    void cancelShortcutPersistsUnderItsOwnKey()
    {
        QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                           QString::fromLatin1(SettingsKeys::Application));
        settings.remove(QStringLiteral("shortcuts/toggleDictation"));
        const GlobalShortcutAction cancel = GlobalShortcutBinder::actionFor(GlobalShortcutRole::Cancel);
        const ShortcutBinding f13 = ShortcutBinding::singleKey(QStringLiteral("F13"));

        FakeSingleKeyShortcutBinder binder(cancel);
        QVERIFY(binder.setShortcut(f13));
        QCOMPARE(settings.value(QStringLiteral("shortcuts/cancelDictation")).toString(), QStringLiteral("key:F13"));
        QVERIFY(!settings.contains(QStringLiteral("shortcuts/toggleDictation")));

        FakeSingleKeyShortcutBinder restored(cancel);
        restored.bind();
        QCOMPARE(restored.shortcut(), f13);
        QVERIFY(restored.setShortcut({}));
        QVERIFY(!settings.contains(QStringLiteral("shortcuts/cancelDictation")));
    }

    // A refused login item has no window of its own to complain in: the
    // controller remembers it so the settings surface can draw the caution.
    void aRefusedLaunchAtLoginIsRememberedUntilOneIsAccepted()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->launchAtLoginError = QStringLiteral("This computer said no.");
        ApplicationController controller(true, platform);
        QVERIFY(!controller.launchAtLoginAccepted());

        QSignalSpy accepted(&controller, &ApplicationController::launchAtLoginAcceptedChanged);
        platform->launchAtLoginError.clear();
        controller.settings()->setLaunchAtLogin(controller.settings()->launchAtLogin());
        QVERIFY(controller.launchAtLoginAccepted());
        QCOMPARE(accepted.count(), 1);
    }

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    void guiLaunchKeepsRunningAfterLastWindowCloses()
    {
        QVERIFY(!quitOnLastWindowClosed(LaunchMode::RunGui));
        QVERIFY(!quitOnLastWindowClosed(LaunchMode::RunDaemon));
    }

    void anUpdateRelaunchDropsStartupActions()
    {
        QCOMPARE(argumentsWithoutStartupActions({QStringLiteral("--daemon"),
                                                 QStringLiteral("--start-listening"),
                                                 QStringLiteral("--format"),
                                                 QStringLiteral("plain"),
                                                 QStringLiteral("--profile"),
                                                 QStringLiteral("custom_deleted"),
                                                 QStringLiteral("--language"),
                                                 QStringLiteral("de"),
                                                 QStringLiteral("--show-settings"),
                                                 QStringLiteral("--show-setup")}),
                 QStringList({QStringLiteral("--daemon")}));
    }

    void audioFileArgumentsOpenTheTranscribePage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        const QString notes = dir.filePath(QStringLiteral("notes.txt"));
        for (const auto &[path, bytes] : {std::pair{audio, QByteArrayLiteral("RIFF\0\0\0\0WAVEfmt ")},
                                          std::pair{notes, QByteArrayLiteral("plain text")}}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(bytes);
        }

        // What a file manager's "Open with" passes: bare paths, of which only
        // the audio is taken.
        const CommandLineDecision opened = parseCommandLine(
            {QStringLiteral("speecher"), audio, notes}, {});
        QCOMPARE(opened.mode, LaunchMode::RunGui);
        QCOMPARE(opened.transcribeFiles, QStringList{audio});

        const CommandLineDecision verb = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("transcribe"), QStringLiteral("later.mp3")}, {});
        QCOMPARE(verb.mode, LaunchMode::RunGui);
        QCOMPARE(verb.transcribeFiles, QStringList{QDir::current().absoluteFilePath(QStringLiteral("later.mp3"))});
    }

    void cancelIsSentToTheRunningSpeecher()
    {
        const CommandLineDecision decision =
            parseCommandLine({QStringLiteral("speecher"), QStringLiteral("cancel")}, {});
        QCOMPARE(decision.mode, LaunchMode::RunCli);
        QCOMPARE(decision.ipcCommand, QStringLiteral("cancel"));
    }

    void transcribeOptionsRunWithoutAWindow()
    {
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const auto parse = [&audio](QStringList options) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("transcribe")} + options
                                        + QStringList{audio},
                                    {});
        };

        const CommandLineDecision decision = parse({QStringLiteral("--cleanup"), QStringLiteral("light"),
                                                    QStringLiteral("--profile"), QStringLiteral("ai-coding"),
                                                    QStringLiteral("--tone"), QStringLiteral("very-casual"),
                                                    QStringLiteral("--refine"), QStringLiteral("none"),
                                                    QStringLiteral("--output"), dir.path(),
                                                    QStringLiteral("--no-vocabulary"), QStringLiteral("--json")});
        QCOMPARE(decision.mode, LaunchMode::TranscribeHeadless);
        QCOMPARE(decision.transcribeFiles, QStringList{audio});
        QCOMPARE(decision.headless.cleanupStrength, std::optional(QStringLiteral("light_cleanup")));
        QCOMPARE(decision.headless.writingProfile, std::optional(QStringLiteral("ai_coding")));
        QCOMPARE(decision.headless.tone, std::optional(QStringLiteral("very_casual")));
        QCOMPARE(decision.headless.refinementProviderId, std::optional(QStringLiteral("none")));
        QCOMPARE(decision.headless.destination, TranscriptDestination::Folder);
        QCOMPARE(decision.headless.folder, dir.path());
        QVERIFY(!decision.headless.applyVocabulary);
        QVERIFY(decision.headless.json);
        QCOMPARE(parse({QStringLiteral("--headless")}).mode, LaunchMode::TranscribeHeadless);
        // Without options the files still open in the window.
        QCOMPARE(parse({}).mode, LaunchMode::RunGui);
        QCOMPARE(parse({QStringLiteral("--grab"), QStringLiteral("shot.png")}).mode, LaunchMode::RunGui);

        QCOMPARE(parse({QStringLiteral("--srt")}).headless.format, TranscriptFormat::Srt);
        QCOMPARE(parse({QStringLiteral("--vtt"), QStringLiteral("--vtt")}).headless.format, TranscriptFormat::WebVtt);
        QCOMPARE(parse({QStringLiteral("--output"), QStringLiteral("NONE")}).headless.destination,
                 TranscriptDestination::None);
        QCOMPARE(parse({QStringLiteral("--cleanup"), QStringLiteral("Medium")}).headless.cleanupStrength,
                 std::optional(QStringLiteral("balanced")));
        // After --, everything is a file, even a name that looks like an option.
        const QString dashed = dir.filePath(QStringLiteral("--memo.wav"));
        QFile dashedFile(dashed);
        QVERIFY(dashedFile.open(QIODevice::WriteOnly));
        dashedFile.close();
        const CommandLineDecision ended = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("transcribe"), QStringLiteral("--json"),
             QStringLiteral("--"), dashed},
            {});
        QCOMPARE(ended.mode, LaunchMode::TranscribeHeadless);
        QCOMPARE(ended.transcribeFiles, QStringList{dashed});

        for (const QStringList &mistake : {QStringList{QStringLiteral("--cleanup"), QStringLiteral("extreme")},
                                           QStringList{QStringLiteral("--frobnicate")},
                                           QStringList{QStringLiteral("-x")},
                                           QStringList{QStringLiteral("--daemon")},
                                           QStringList{QStringLiteral("--srt"), QStringLiteral("--vtt")},
                                           QStringList{QStringLiteral("--vtt"), QStringLiteral("--raw")},
                                           QStringList{QStringLiteral("--output"), dir.filePath(QStringLiteral("nowhere"))}}) {
            const CommandLineDecision refused = parse(mistake);
            QCOMPARE(refused.mode, LaunchMode::Exit);
            QCOMPARE(refused.exitCode, 2);
        }
        QCOMPARE(parseCommandLine({QStringLiteral("speecher"), QStringLiteral("transcribe"), QStringLiteral("--json")}, {})
                     .exitCode,
                 2);
        QCOMPARE(parseCommandLine({QStringLiteral("speecher"), QStringLiteral("transcribe"), QStringLiteral("--json"),
                                   dir.filePath(QStringLiteral("missing.wav"))},
                                  {})
                     .exitCode,
                 2);
    }

    // listen takes transcribe's choices and --until-silence, with or without
    // its seconds; the --profile and --language it reads are its own.
    void listenTakesTranscribesChoicesAndASilenceTimeout()
    {
        const auto parse = [](QStringList options) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("listen")} + options, {});
        };

        const CommandLineDecision decision =
            parse({QStringLiteral("--until-silence"), QStringLiteral("1.5"), QStringLiteral("--refine"),
                   QStringLiteral("none"), QStringLiteral("--profile"), QStringLiteral("ai-coding"),
                   QStringLiteral("--language"), QStringLiteral("de"), QStringLiteral("--tone"),
                   QStringLiteral("formal"), QStringLiteral("--raw"), QStringLiteral("--json")});
        QCOMPARE(decision.mode, LaunchMode::ListenHeadless);
        QCOMPARE(decision.untilSilenceMs, std::optional(1500));
        QCOMPARE(decision.headless.refinementProviderId, std::optional(QStringLiteral("none")));
        QCOMPARE(decision.headless.writingProfile, std::optional(QStringLiteral("ai_coding")));
        QCOMPARE(decision.headless.spokenLanguage, std::optional(QStringLiteral("de")));
        QCOMPARE(decision.headless.tone, std::optional(QStringLiteral("formal")));
        QVERIFY(decision.headless.raw);
        QVERIFY(decision.headless.json);
        QVERIFY(!decision.sessionOverrides.writingProfile);
        QVERIFY(!decision.sessionOverrides.spokenLanguage);

        QCOMPARE(parse({}).mode, LaunchMode::ListenHeadless);
        QCOMPARE(parse({}).untilSilenceMs, std::nullopt);

        QTemporaryDir dir;
        const QString vocabulary = dir.filePath(QStringLiteral("terms.txt"));
        QFile vocabularyFile(vocabulary);
        QVERIFY(vocabularyFile.open(QIODevice::WriteOnly));
        vocabularyFile.write("readSharedChoice\nSpeecher CLI\n");
        vocabularyFile.close();
        QCOMPARE(parse({QStringLiteral("--vocab-file"), vocabulary}).headless.addedVocabulary,
                 QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher CLI")}));
        QCOMPARE(parse({QStringLiteral("--vocab-file"), vocabulary, QStringLiteral("--vocab-file"), vocabulary})
                     .headless.addedVocabulary,
                 QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher CLI"),
                              QStringLiteral("readSharedChoice"), QStringLiteral("Speecher CLI")}));
        QCOMPARE(parse({QStringLiteral("--until-silence")}).untilSilenceMs, std::optional(2000));
        QCOMPARE(parse({QStringLiteral("--until-silence"), QStringLiteral("--no-vocabulary")}).untilSilenceMs,
                 std::optional(2000));

        for (const QStringList &mistake : {QStringList{QStringLiteral("--until-silence"), QStringLiteral("soon")},
                                           QStringList{QStringLiteral("--until-silence"), QStringLiteral("0")},
                                           QStringList{QStringLiteral("--until-silence"), QStringLiteral("0.05")},
                                           QStringList{QStringLiteral("--until-silence"), QStringLiteral("nan")},
                                           QStringList{QStringLiteral("--until-silence"), QStringLiteral("7200")},
                                           QStringList{QStringLiteral("--stdout")},
                                           QStringList{QStringLiteral("--output"), QStringLiteral("none")},
                                           QStringList{QStringLiteral("memo.wav")},
                                           QStringList{QStringLiteral("--vocab-file")},
                                           QStringList{QStringLiteral("--vocab-file"), dir.filePath(QStringLiteral("missing.txt"))},
                                           QStringList{QStringLiteral("--cleanup"), QStringLiteral("extreme")}}) {
            const CommandLineDecision refused = parse(mistake);
            QCOMPARE(refused.mode, LaunchMode::Exit);
            QCOMPARE(refused.exitCode, 2);
        }

        // A negative number is the seconds, refused for its range.
        std::ostringstream usage;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        QCOMPARE(parse({QStringLiteral("--until-silence"), QStringLiteral("-1")}).exitCode, 2);
        QVERIFY2(QString::fromStdString(usage.str())
                     .startsWith(QStringLiteral("--until-silence takes seconds from 0.1 to 3600, not -1\n")),
                 usage.str().c_str());
    }

    // A custom tone or level is named by its id without custom_, with - for _.
    void transcribeTakesCustomTonesAndLevels()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.customTones = {
            {QStringLiteral("custom_very_terse"), QStringLiteral("Very terse"), QStringLiteral("Short.")}};
        draft.refinement.customCleanupLevels = {{QStringLiteral("custom_notes"), QStringLiteral("Notes"),
                                                 QStringLiteral("balanced"), QString()}};
        settings.applySnapshot(draft);
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const CommandLineDecision decision = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("transcribe"), QStringLiteral("--tone"),
             QStringLiteral("very-terse"), QStringLiteral("--cleanup"), QStringLiteral("notes"), audio},
            {});
        QCOMPARE(decision.headless.tone, std::optional(QStringLiteral("custom_very_terse")));
        QCOMPARE(decision.headless.cleanupStrength, std::optional(QStringLiteral("custom_notes")));
        settings.raw().clear();
    }

    // By its name with - between words, as toggle and start take it.
    void transcribeTakesACustomProfile()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.writingProfiles.append({QStringLiteral("custom_stand_up"), QStringLiteral("balanced"),
                                                 QStringLiteral("none"), QString(), QStringLiteral("Stand up")});
        settings.applySnapshot(draft);
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const CommandLineDecision decision = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("transcribe"), QStringLiteral("--profile"),
             QStringLiteral("stand-up"), audio},
            {});
        QCOMPARE(decision.headless.writingProfile, std::optional(QStringLiteral("custom_stand_up")));
        settings.raw().clear();
    }

    void toggleStartAndTranscribeTakeASpokenLanguage()
    {
        const auto parse = [](const QStringList &arguments) {
            return parseCommandLine(QStringList{QStringLiteral("speecher")} + arguments, {});
        };
        QCOMPARE(parse({QStringLiteral("toggle"), QStringLiteral("--language"), QStringLiteral("DE")})
                     .sessionOverrides.spokenLanguage,
                 std::optional(QStringLiteral("de")));
        QCOMPARE(parse({QStringLiteral("start"), QStringLiteral("--language"), QStringLiteral("auto")})
                     .sessionOverrides.spokenLanguage,
                 std::optional(QStringLiteral("auto")));
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("memo.wav"));
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(parse({QStringLiteral("transcribe"), QStringLiteral("--language"), QStringLiteral("fr"), audio})
                     .headless.spokenLanguage,
                 std::optional(QStringLiteral("fr")));
        for (const QStringList &mistake :
             {QStringList{QStringLiteral("toggle"), QStringLiteral("--language"), QStringLiteral("klingon")},
              QStringList{QStringLiteral("start"), QStringLiteral("--language")},
              QStringList{QStringLiteral("status"), QStringLiteral("--language"), QStringLiteral("de")}}) {
            const CommandLineDecision refused = parse(mistake);
            QCOMPARE(refused.mode, LaunchMode::Exit);
            QCOMPARE(refused.exitCode, 2);
        }
    }

    void toggleAndStartTakeAWritingProfile()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        // Created as "Stand up", since renamed.
        draft.refinement.writingProfiles.append({QStringLiteral("custom_stand_up"), QStringLiteral("balanced"),
                                                 QStringLiteral("none"), QString(), QStringLiteral("Daily sync")});
        settings.applySnapshot(draft);
        const auto parse = [](const QStringList &arguments) {
            return parseCommandLine(QStringList{QStringLiteral("speecher")} + arguments, {});
        };
        const auto profile = [&parse](const QString &value) {
            return parse({QStringLiteral("toggle"), QStringLiteral("--profile"), value})
                .sessionOverrides.writingProfile;
        };

        const CommandLineDecision toggle = parse({QStringLiteral("toggle"), QStringLiteral("--profile"),
                                                  QStringLiteral("Email")});
        QCOMPARE(toggle.mode, LaunchMode::RunCli);
        QCOMPARE(toggle.sessionOverrides.writingProfile, std::optional(WritingProfile::Email));
        const CommandLineDecision start = parse({QStringLiteral("start"), QStringLiteral("--profile"),
                                                 QStringLiteral("AI-Coding"), QStringLiteral("--format"),
                                                 QStringLiteral("html")});
        QCOMPARE(start.sessionOverrides.writingProfile, std::optional(WritingProfile::AiCoding));
        QCOMPARE(start.sessionOverrides.outputFormat, std::optional(OutputFormat::Html));
        QCOMPARE(profile(QStringLiteral("ai coding")), std::optional(WritingProfile::AiCoding));
        QCOMPARE(profile(QStringLiteral("Daily Sync")), std::optional(QStringLiteral("custom_stand_up")));
        QCOMPARE(profile(QStringLiteral("daily-sync")), std::optional(QStringLiteral("custom_stand_up")));
        QVERIFY(!profile(QStringLiteral("stand-up")));
        // What toggle and start pass on to the daemon they start.
        QCOMPARE(parse({QStringLiteral("--daemon"), QStringLiteral("--start-listening"), QStringLiteral("--profile"),
                        QStringLiteral("custom_stand_up")})
                     .sessionOverrides.writingProfile,
                 std::optional(QStringLiteral("custom_stand_up")));

        // A custom profile named like a built-in makes that name ambiguous.
        draft.refinement.writingProfiles.append({QStringLiteral("custom_email"), QStringLiteral("balanced"),
                                                 QStringLiteral("none"), QString(), QStringLiteral("Email")});
        settings.applySnapshot(draft);
        for (const QStringList &mistake : {QStringList{QStringLiteral("toggle"), QStringLiteral("--profile"),
                                                       QStringLiteral("email")},
                                           QStringList{QStringLiteral("toggle"), QStringLiteral("--profile"),
                                                       QStringLiteral("poetry")},
                                           QStringList{QStringLiteral("start"), QStringLiteral("--profile")},
                                           QStringList{QStringLiteral("status"), QStringLiteral("--profile"),
                                                       QStringLiteral("work")}}) {
            const CommandLineDecision refused = parse(mistake);
            QCOMPARE(refused.mode, LaunchMode::Exit);
            QCOMPARE(refused.exitCode, 2);
        }
        settings.raw().clear();
    }

    void quitIsAClientCommand()
    {
        const CommandLineDecision decision = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("quit")}, {});
        QCOMPARE(decision.mode, LaunchMode::RunCli);
        QCOMPARE(decision.ipcCommand, QStringLiteral("quit"));

        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QSignalSpy requested(&controller, &ApplicationController::quitRequested);
        controller.handleIpcCommand(QStringLiteral("quit"), {}, nullptr);
        QCOMPARE(requested.count(), 1);
    }

#endif

#ifdef Q_OS_LINUX
    void setupAssistantPutsTheGlobalShortcutBeforeFinish()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        SetupAssistant assistant(&controller);

        const QStringList titles = assistant.pageTitles();
        QCOMPARE(titles,
                 QStringList({QStringLiteral("Welcome"),
                              QStringLiteral("Transcription"),
                              QStringLiteral("Microphone"),
                              QStringLiteral("Accessibility"),
                              QStringLiteral("Text delivery"),
                              QStringLiteral("Refinement"),
                              QStringLiteral("Global Shortcut"),
                              QStringLiteral("Ready to dictate")}));
        QVERIFY(titles.indexOf(QStringLiteral("Global Shortcut"))
                < titles.indexOf(QStringLiteral("Ready to dictate")));

        WelcomeSetupPage *welcome = nullptr;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            if (auto *page = dynamic_cast<WelcomeSetupPage *>(widget)) {
                welcome = page;
                break;
            }
        }
        QVERIFY(welcome);
        bool mentionsShortcut = false;
        for (const QLabel *label : welcome->findChildren<QLabel *>()) {
            mentionsShortcut = mentionsShortcut
                || label->text().contains(QStringLiteral("and a Global Shortcut"));
        }
        QVERIFY(mentionsShortcut);

        // The shortcut page is also a settings card control; as an assistant
        // page it keeps the same margins as the pages around it.
        LinuxGlobalShortcutSetupPage *shortcut = nullptr;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            if (auto *page = dynamic_cast<LinuxGlobalShortcutSetupPage *>(widget)) {
                shortcut = page;
                break;
            }
        }
        if (!shortcut) {
            // CI-only failure diagnostics: show what the assistant's widget
            // tree actually holds when the page cannot be found.
            for (const QWidget *widget : assistant.findChildren<QWidget *>()) {
                qWarning("child: %s objectName=%s",
                         widget->metaObject()->className(),
                         qPrintable(widget->objectName()));
            }
        }
        QVERIFY(shortcut);
        QCOMPARE(shortcut->layout()->contentsMargins(), welcome->layout()->contentsMargins());
        QCOMPARE(shortcut->layout()->contentsMargins().left(), setupPageMargin());

        // The assistant keeps the application palette rather than retuning a
        // role to fight its own style's separator.
        QVERIFY(!assistant.testAttribute(Qt::WA_SetPalette));
        QCOMPARE(assistant.palette().color(QPalette::Mid),
                 QApplication::palette().color(QPalette::Mid));
    }

    void setupAssistantHidesSkipWhileStepsAreIncomplete()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        SetupAssistant assistant(&controller);
        assistant.show();
        QCoreApplication::processEvents();

        QAbstractButton *skip = nullptr;
        for (QAbstractButton *button : assistant.findChildren<QAbstractButton *>()) {
            if (button->text() == QStringLiteral("Skip setup")) {
                skip = button;
                break;
            }
        }
        QVERIFY(skip);
        // On this fake platform the steps are incomplete (no provider signed
        // in, no microphone input yet), so skipping is not offered on any
        // page, the last one included.
        QVERIFY(!skip->isVisible());
        const int lastPage = assistant.pageTitles().indexOf(QStringLiteral("Ready to dictate"));
        QCOMPARE(lastPage, assistant.pageTitles().size() - 1);
#ifdef SPEECHER_WITH_KASSISTANT
        for (int step = 0; step < lastPage; ++step) {
            assistant.next();
        }
#else
        assistant.setCurrentId(assistant.pageIds().at(lastPage));
#endif
        QCoreApplication::processEvents();
        QVERIFY(!skip->isVisible());
    }

    void setupAssistantReturnsToTheFirstFailedStepInsteadOfFinishing()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        controller.settings()->setSetupCompleted(false);
        SetupAssistant assistant(&controller);
        auto *transcription = assistant.findChild<SpeechProviderSetupPage *>();
        QVERIFY(transcription);
        assistant.show();

        // Walked with Next, as a person would: QWizard goes back only through
        // pages it visited.
        const int lastPage = assistant.pageTitles().size() - 1;
        for (int step = 0; step < lastPage; ++step) {
            assistant.next();
        }
        QCoreApplication::processEvents();

        // Pressing Finish must land the user back on the first unfinished
        // step rather than marking setup complete.
        QVERIFY(QMetaObject::invokeMethod(&assistant, "accept"));
        QCoreApplication::processEvents();

        QVERIFY(!controller.settings()->setupCompleted());
        QVERIFY(assistant.isVisible());
        // With no sign-in and no downloaded model, Transcription is the
        // first unfinished step.
        if (transcription->ready()) QSKIP("A sign-in on this computer opens Transcription.");
        QVERIFY(transcription->isVisible());
    }

    void globalShortcutSinglePageOnlyShowsTheShortcutPage()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        SetupAssistant assistant(&controller, SetupAssistantPage::GlobalShortcut);
        assistant.show();

        QCOMPARE(assistant.pageTitles(), QStringList({QStringLiteral("Global Shortcut")}));
        int visibleSetupPages = 0;
        for (QWidget *widget : assistant.findChildren<QWidget *>()) {
            // The shortcut step's Cancel and Pause rows are pages of their own,
            // nested inside it.
            bool nested = false;
            for (QWidget *ancestor = widget->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
                nested = nested || dynamic_cast<LinuxGlobalShortcutSetupPage *>(ancestor);
            }
            const bool setupPage = (dynamic_cast<LinuxGlobalShortcutSetupPage *>(widget) && !nested)
                || dynamic_cast<WelcomeSetupPage *>(widget)
                || dynamic_cast<MicrophoneSetupPage *>(widget);
            visibleSetupPages += setupPage && widget->isVisible();
        }
        QCOMPARE(visibleSetupPages, 1);
    }

    void globalShortcutPageEditsNativeShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const QKeySequence initial(Qt::META | Qt::ALT | Qt::Key_D);
        platform->binder->publishShortcut(initial);
        LinuxGlobalShortcutSetupPage page(controller);

        auto *capture = page.findChild<QPushButton *>(QStringLiteral("globalShortcutCapture"));
        auto *binding = page.findChild<QLabel *>(QStringLiteral("globalShortcutBinding"));
        auto *description = page.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"));
        auto *reset = page.findChild<QPushButton *>(QStringLiteral("resetGlobalShortcut"));
        QVERIFY(capture && binding && description && reset);
        // Idle, the row shows the bound combination beside a button that says
        // what pressing it does.
        QCOMPARE(binding->text(), initial.toString(QKeySequence::NativeText));
        QCOMPARE(capture->text(), QStringLiteral("Change…"));
        QCOMPARE(description->text(), QStringLiteral("Start or stop dictation from anywhere."));
        QVERIFY(reset->isHidden());

        page.show();
        capture->click();
        QCOMPARE(capture->text(), QStringLiteral("Cancel"));
        QCOMPARE(description->text(),
                 QStringLiteral("Press a key combination, or a single key such as Right Alt or F13."));
        // Recording must not fire the bound shortcut.
        QCOMPARE(platform->binder->suspendCount, 1);
        const QKeySequence chosen(Qt::CTRL | Qt::ALT | Qt::Key_Space);
        QTest::keyClick(capture, Qt::Key_Space, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(controller.globalShortcut().combination(), chosen);
        QCOMPARE(platform->binder->resumeCount, 1);
        QCOMPARE(binding->text(), QStringLiteral("Ctrl+Alt+Space"));
        QCOMPARE(description->text(), QStringLiteral("Shortcut set to Ctrl+Alt+Space. Try it now."));

        // Escape abandons the capture and keeps the bound combination.
        capture->click();
        QTest::keyClick(capture, Qt::Key_Escape);
        QCOMPARE(controller.globalShortcut().combination(), chosen);
        QCOMPARE(capture->text(), QStringLiteral("Change…"));

        platform->binder->setShortcutError = QStringLiteral("That shortcut is already in use.");
        capture->click();
        QTest::keyClick(capture, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("shortcutCaptureFeedback"))->text(),
                 QStringLiteral("That shortcut is already in use."));
        // The failed capture leaves the row naming what is still bound.
        QCOMPARE(binding->text(), QStringLiteral("Ctrl+Alt+Space"));

        // Reset goes back to the default, and then has nothing left to do.
        platform->binder->setShortcutError.clear();
        QVERIFY(!reset->isHidden());
        QCOMPARE(reset->text(), QStringLiteral("Reset to Meta+Alt+D"));
        reset->click();
        QCOMPARE(controller.globalShortcut().combination(), initial);
        QVERIFY(reset->isHidden());
    }

    void globalShortcutPageWaitsForPortalSupportAndShowsItsResult()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutSupportKnown = false;
        platform->binder->shortcutsSupported = false;
        platform->binder->desktopChooser = true;
        LinuxGlobalShortcutSetupPage page(controller);

        auto *portal = page.findChild<QWidget *>(QStringLiteral("portalShortcut"));
        auto *status = page.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"));
        QVERIFY(portal);
        QVERIFY(!portal->isHidden());
        QVERIFY(status);
        QCOMPARE(status->text(), QStringLiteral("Checking your desktop…"));

        QPushButton *chooseShortcut = nullptr;
        for (QPushButton *button : page.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Choose shortcut")) {
                chooseShortcut = button;
                break;
            }
        }
        QVERIFY(chooseShortcut);
        QVERIFY(!chooseShortcut->isEnabled());

        platform->binder->publishSupport(true, true);
        QVERIFY(chooseShortcut->isEnabled());
        chooseShortcut->click();
        QCOMPARE(platform->binder->registerCount, 1);

        const QString result = QStringLiteral("Ctrl+Alt+Space");
        platform->binder->publishShortcut(QKeySequence(result));
        platform->binder->publishRegistrationResult(true, result);
        QCOMPARE(status->text(),
                 QStringLiteral("Shortcut set to Ctrl+Alt+Space. Try it now."));
    }

    void globalShortcutPageClearsASingleKeyOnPortalDesktops()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->desktopChooser = true;
        platform->binder->publishShortcut(ShortcutBinding::singleKey(QStringLiteral("F13")));
        LinuxGlobalShortcutSetupPage page(controller);

        auto *capture = page.findChild<QPushButton *>(QStringLiteral("globalShortcutCapture"));
        auto *binding = page.findChild<QLabel *>(QStringLiteral("globalShortcutBinding"));
        auto *clear = page.findChild<QPushButton *>(QStringLiteral("clearGlobalShortcut"));
        QVERIFY(capture && binding && clear);
        // Beside the desktop's Choose shortcut, the capture names what it records.
        QCOMPARE(capture->text(), QStringLiteral("Set single key"));
        QVERIFY(!clear->isHidden());

        clear->click();
        QVERIFY(!controller.globalShortcut().isSingleKey());
        QCOMPARE(binding->text(), QStringLiteral("Not set"));
        QVERIFY(clear->isHidden());
    }

    void globalShortcutPageKeepsPortalFailureAfterRestoringTheOldShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->desktopChooser = true;
        const QKeySequence existing(Qt::CTRL | Qt::ALT | Qt::Key_Space);
        platform->binder->publishShortcut(existing);
        LinuxGlobalShortcutSetupPage page(controller);
        auto *status = page.findChild<QLabel *>(QStringLiteral("globalShortcutStatus"));
        QVERIFY(status);

        platform->binder->publishRegistrationResult(
            false, QStringLiteral("Setup was cancelled. Try again."));
        platform->binder->publishShortcut(existing);

        QCOMPARE(status->text(), QStringLiteral("Setup was cancelled. Try again."));
    }

    void trayShortcutNoteOnlyClaimsAnIconWhereATrayExists()
    {
        QVERIFY(linuxTrayShortcutNote(true).contains(QStringLiteral("system tray")));
        QVERIFY(!linuxTrayShortcutNote(false).contains(QStringLiteral("system tray")));
        QVERIFY(linuxTrayShortcutNote(false)
                    .contains(QStringLiteral("while Speecher is running")));
    }

    void globalShortcutPageExplainsTheTrayIcon()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        LinuxGlobalShortcutSetupPage page(controller);

        auto *note = page.findChild<QLabel *>(QStringLiteral("globalShortcutTrayNote"));
        QVERIFY(note);
        QVERIFY(!note->isHidden());
        QCOMPARE(note->text(),
                 linuxTrayShortcutNote(QSystemTrayIcon::isSystemTrayAvailable()));

        // The manual command starts Speecher itself, so the running-app
        // caveat is withheld on desktops that cannot register a shortcut.
        platform->binder->publishSupport(true, false);
        QVERIFY(note->isHidden());
    }

    void finishPageListsWhatWasSetUpWhenEveryStepIsDone()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FinishSetupPage page(controller);
        page.setSteps({
            {QStringLiteral("Transcription"), true, QStringLiteral("Transcription — ChatGPT Codex")},
            {QStringLiteral("Accessibility"), true, QString()},
            {QStringLiteral("Microphone"), true, QStringLiteral("Microphone — Yeti Stereo Microphone")},
        });
        page.show();
        QCoreApplication::processEvents();

        QStringList shown;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            if (label->isVisible()) {
                shown << label->text();
            }
        }
        QVERIFY(shown.contains(QStringLiteral("Setup is complete.")));
        QVERIFY(shown.contains(QStringLiteral("Transcription — ChatGPT Codex")));
        QVERIFY(shown.contains(QStringLiteral("Microphone — Yeti Stereo Microphone")));
        // A step with nothing chosen has nothing to report back.
        QVERIFY(!shown.contains(QStringLiteral("Accessibility")));
        QVERIFY(!shown.contains(QStringLiteral("A few steps still need attention:")));
    }

    void finishPageReportsTheRefinementVerdict()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FinishSetupPage page(controller);
        SetupStepStatus refinement{QStringLiteral("Refinement"), true, QStringLiteral("Refinement — OpenAI")};
        refinement.verdict = QStringLiteral("Not signed in");
        refinement.verdictReady = false;
        page.setSteps({refinement});
        page.show();
        QCoreApplication::processEvents();

        QStringList shown;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            if (label->isVisible()) {
                shown << label->text();
            }
        }
        QVERIFY(shown.contains(QStringLiteral("Not signed in")));
        QVERIFY(!shown.contains(QStringLiteral("Ready")));
    }

    void finishPageShowsADownloadThatIsStillGoing()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const LocalModel &model = *findLocalModel(QStringLiteral("moonshine-small"));
        FinishSetupPage page(controller);
        page.setSteps({
            {QStringLiteral("Transcription"), true, QStringLiteral("Transcription — Moonshine Small, on this computer"),
             model.id},
        });
        page.show();
        QCoreApplication::processEvents();
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("finishDownloadNotice"))->isHidden());
        QVERIFY(!page.findChild<QWidget *>(QStringLiteral("finishDownloadProgress")));

        controller.localSetup()->download(model);
        QCoreApplication::processEvents();
        QVERIFY(!page.findChild<QWidget *>(QStringLiteral("finishDownloadNotice"))->isHidden());
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("finishDownloadProgress")));
        QStringList shown;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            if (label->isVisible()) {
                shown << label->text();
            }
        }
        QVERIFY(shown.contains(QStringLiteral("Setup is complete except for the speech model download.")));

        page.findChild<QPushButton *>(QStringLiteral("finishDownloadCancel"))->click();
        QCoreApplication::processEvents();
        QVERIFY(!controller.localModelStore()->isDownloading(model.id));
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("finishDownloadNotice"))->isHidden());
    }

    void finishPageListsUnfinishedStepsAndGoesBackToThem()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FinishSetupPage page(controller);
        page.setSteps({
            {QStringLiteral("Transcription"), false,
             QStringLiteral("ChatGPT Codex is no longer signed in.")},
            {QStringLiteral("Microphone"), true,
             QStringLiteral("Microphone — Yeti Stereo Microphone")},
        });
        page.show();
        QCoreApplication::processEvents();

        QStringList shown;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            if (label->isVisible()) {
                shown << label->text();
            }
        }
        QVERIFY(shown.contains(QStringLiteral("A few steps still need attention:")));
        QVERIFY(shown.contains(QStringLiteral("Transcription")));
        QVERIFY(shown.contains(QStringLiteral("ChatGPT Codex is no longer signed in.")));
        QVERIFY(shown.contains(
            QStringLiteral("Finish becomes available once every step above is resolved.")));
        // A blocked page reports what is left, not what already worked.
        QVERIFY(!shown.contains(QStringLiteral("Setup is complete.")));
        QVERIFY(!shown.contains(QStringLiteral("Microphone — Yeti Stereo Microphone")));

        QPushButton *goToStep = nullptr;
        for (QPushButton *button : page.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("Go to step")) {
                goToStep = button;
                break;
            }
        }
        QVERIFY(goToStep);
        QSignalSpy selected(&page, &FinishSetupPage::stepSelected);
        goToStep->click();
        QCOMPARE(selected.count(), 1);
        // The index is into the list the page was given, so the wizard can
        // resolve it back to the page that step belongs to.
        QCOMPARE(selected.first().first().toInt(), 0);
    }

    void finishPageExplainsTheTrayIcon()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->publishShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        FinishSetupPage page(controller);

        auto *note = page.findChild<QLabel *>(QStringLiteral("finishTrayNote"));
        QVERIFY(note);
        QVERIFY(!note->isHidden());
        QCOMPARE(note->text(),
                 linuxTrayShortcutNote(QSystemTrayIcon::isSystemTrayAvailable()));
    }

    void finishPageHidesTheTrayNoteNextToTheManualCommand()
    {
        // Supported desktop, but no shortcut bound: the page recommends the
        // manual command, which starts Speecher itself.
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FinishSetupPage page(controller);

        auto *note = page.findChild<QLabel *>(QStringLiteral("finishTrayNote"));
        QVERIFY(note);
        QVERIFY(note->isHidden());
        QVERIFY(!page.findChild<QLabel *>(QStringLiteral("finishGlobalShortcutCommand"))
                     ->isHidden());
    }

    void globalShortcutPageShowsOnlyManualSetupWhenUnsupported()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutsSupported = false;
        platform->binder->unsupported = QStringLiteral("Internal binder detail");
        LinuxGlobalShortcutSetupPage page(controller);

        auto *manual = page.findChild<QWidget *>(QStringLiteral("manualShortcut"));
        QVERIFY(manual);
        QVERIFY(!manual->isHidden());
        QVERIFY(page.findChild<QWidget *>(QStringLiteral("portalShortcut"))->isHidden());
        // The capture stays on offer: a single key needs no desktop service.
        auto *captureBlock = page.findChild<QWidget *>(QStringLiteral("shortcutCapture"));
        QVERIFY(captureBlock);
        QVERIFY(!captureBlock->isHidden());
        // While it waits, the row says only a single key can be recorded.
        page.show();
        page.findChild<QPushButton *>(QStringLiteral("globalShortcutCapture"))->click();
        QCOMPARE(captureBlock->findChild<QLabel *>(QStringLiteral("globalShortcutStatus"))->text(),
                 QStringLiteral("Press a single key, such as Right Alt or F13, to use on its own."));
        QCOMPARE(page.findChildren<QGroupBox *>().size(), 0);

        bool hasInstruction = false;
        bool hasInternalDetail = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasInstruction = hasInstruction
                || label->text() == linuxGlobalShortcutManualInstruction();
            hasInternalDetail = hasInternalDetail
                || label->text() == QStringLiteral("Internal binder detail");
        }
        QVERIFY(hasInstruction);
        QVERIFY(!hasInternalDetail);
        auto *command = page.findChild<QLabel *>(QStringLiteral("globalShortcutCommand"));
        QVERIFY(command);
        QVERIFY(command->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QVERIFY(!command->text().isEmpty());
    }

    void finishPageNamesTheBoundGlobalShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        // The closing instruction follows the activation mode.
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::Hybrid);
        platform->binder->publishShortcut(
            QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        FinishSetupPage page(controller);
        page.show();
        QCoreApplication::processEvents();

        bool hasInstruction = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasInstruction = hasInstruction
                || label->text() == QStringLiteral(
                    "To dictate, tap Meta+Alt+D to toggle, or hold it to dictate until release.");
        }
        QVERIFY(hasInstruction);
    }

    void finishPageInstructionFollowsThePushToTalkActivationMode()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::PushToTalk);
        platform->binder->publishShortcut(
            QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        FinishSetupPage page(controller);
        page.show();
        QCoreApplication::processEvents();

        auto *status = page.findChild<QLabel *>(QStringLiteral("finishGlobalShortcutStatus"));
        QVERIFY(status);
        QCOMPARE(status->text(),
                 QStringLiteral("To dictate, hold Meta+Alt+D while you speak."));
    }

    void finishPageShowsTheManualGlobalShortcutCommand()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutsSupported = false;
        FinishSetupPage page(controller);
        page.show();
        QCoreApplication::processEvents();

        bool hasInstruction = false;
        for (const QLabel *label : page.findChildren<QLabel *>()) {
            hasInstruction = hasInstruction
                || label->text() == linuxGlobalShortcutManualInstruction();
        }
        QVERIFY(hasInstruction);
        auto *command = page.findChild<QLabel *>(
            QStringLiteral("finishGlobalShortcutCommand"));
        QVERIFY(command);
        QCOMPARE(command->text(), linuxGlobalShortcutCommand());
        QVERIFY(!command->isHidden());
    }

    void finishPageExplainsWhenASupportedShortcutIsUnset()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->shortcutsSupported = true;
        FinishSetupPage page(controller);

        auto *status = page.findChild<QLabel *>(QStringLiteral("finishGlobalShortcutStatus"));
        auto *command = page.findChild<QLabel *>(
            QStringLiteral("finishGlobalShortcutCommand"));
        QVERIFY(status);
        QCOMPARE(status->text(), QStringLiteral(
            "No Global Shortcut is set yet. Go back to set one, or bind this command yourself:"));
        QVERIFY(command);
        QVERIFY(!command->isHidden());
    }

    void finishPageUpdatesWhenThePortalPublishesAShortcut()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        // The closing instruction follows the activation mode.
        controller.settings()->setShortcutActivationMode(ShortcutActivationMode::Hybrid);
        FinishSetupPage page(controller);
        auto *status = page.findChild<QLabel *>(QStringLiteral("finishGlobalShortcutStatus"));
        auto *command = page.findChild<QLabel *>(
            QStringLiteral("finishGlobalShortcutCommand"));
        QVERIFY(status);
        QVERIFY(command);

        platform->binder->publishShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));

        QCOMPARE(status->text(), QStringLiteral(
            "To dictate, tap Meta+Alt+D to toggle, or hold it to dictate until release."));
        QVERIFY(command->isHidden());
    }

    void globalShortcutInstructionCommandMatchesTheInstallation()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());

        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     QString(),
                     QStringLiteral("/opt/Speecher Current/bin/speecher")),
                 QStringLiteral("\"/opt/Speecher Current/bin/speecher\" toggle"));
        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     QStringLiteral("/opt/Speecher Current.AppImage"),
                     QStringLiteral("/tmp/.mount/usr/bin/speecher")),
                 QStringLiteral("\"/opt/Speecher Current.AppImage\" toggle"));

        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral(".local/bin"))));
        const QString appImage = home.filePath(QStringLiteral("Speecher.AppImage"));
        QFile source(appImage);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.close();
        const QString link = home.filePath(QStringLiteral(".local/bin/speecher"));
        const QString staleImage = home.filePath(QStringLiteral("Old Speecher.AppImage"));
        QFile stale(staleImage);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        QVERIFY(QFile::link(staleImage, link));
        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     appImage,
                     QStringLiteral("/tmp/.mount/usr/bin/speecher")),
                 QStringLiteral("\"%1\" toggle").arg(appImage));

        QVERIFY(QFile::remove(link));
        QVERIFY(QFile::link(appImage, link));
        QCOMPARE(globalShortcutInstructionCommand(
                     home.path(),
                     appImage,
                     QStringLiteral("/tmp/.mount/usr/bin/speecher")),
                 QStringLiteral("\"%1\" toggle").arg(link));
    }

    void appImageIntegrationRemovalUndoesTheInstallAndReportsIt()
    {
        const QByteArray oldAppImage = qgetenv("APPIMAGE");
        const auto restoreEnvironment = qScopeGuard([oldAppImage] {
            if (oldAppImage.isNull()) {
                qunsetenv("APPIMAGE");
            } else {
                qputenv("APPIMAGE", oldAppImage);
            }
        });
        qunsetenv("APPIMAGE");

        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QDir home(root.filePath(QStringLiteral("home")));
        QVERIFY(QDir().mkpath(home.path()));
        // The AppImage mount: usr/bin/speecher with the desktop file and icon
        // two levels up, as installAppImageIntegration expects.
        const QString appDir = root.filePath(QStringLiteral("mount"));
        const QString binDir = appDir + QStringLiteral("/usr/bin");
        QVERIFY(QDir().mkpath(binDir));
        const auto writeFile = [](const QString &path, const QByteArray &contents) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                return false;
            }
            return file.write(contents) == contents.size();
        };
        QVERIFY(writeFile(appDir + QStringLiteral("/io.github.firemonster612.speecher.desktop"),
                          "[Desktop Entry]\nExec=speecher\n"));
        QVERIFY(writeFile(appDir + QStringLiteral("/io.github.firemonster612.speecher.svg"),
                          "<svg/>"));
        const QString appImage = root.filePath(QStringLiteral("Speecher.AppImage"));
        QVERIFY(writeFile(appImage, "image"));

        QString error;
        QVERIFY2(installAppImageIntegration(home.path(), appImage, binDir, &error), qPrintable(error));
        const QString desktopFile = home.filePath(
            QStringLiteral(".local/share/applications/io.github.firemonster612.speecher.desktop"));
        const QString icon = home.filePath(
            QStringLiteral(".local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"));
        const QString link = home.filePath(QStringLiteral(".local/bin/speecher"));
        const QString helper = home.filePath(
            QStringLiteral(".local/share/speecher/libexec/speecher-ydotool-setup"));
        QVERIFY(QDir().mkpath(QFileInfo(helper).dir().path()));
        QVERIFY(writeFile(helper, "helper"));
        QVERIFY(QFile::exists(desktopFile));
        QVERIFY(QFile::exists(icon));
        QVERIFY(QFile::exists(helper));
        QVERIFY(QFileInfo(link).isSymLink());

        DesktopIntegrationRemoval removal = removeAppImageIntegration(home.path());
        QCOMPARE(removal.removed,
                 QStringList({QStringLiteral("app menu entry"),
                              QStringLiteral("speecher command"),
                              QStringLiteral("app icon"),
                              QStringLiteral("local ydotool setup helper")}));
        QVERIFY(removal.absent.isEmpty());
        QVERIFY(removal.failed.isEmpty());
        QVERIFY(!QFile::exists(desktopFile));
        QVERIFY(!QFile::exists(icon));
        QVERIFY(!QFile::exists(helper));
        QVERIFY(!QFileInfo(link).isSymLink() && !QFile::exists(link));
        // The program file is the user's to delete.
        QVERIFY(QFile::exists(appImage));

        // A second run finds nothing and says so rather than failing.
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.removed.isEmpty());
        QCOMPARE(removal.absent.size(), 4);
        QVERIFY(removal.failed.isEmpty());

        // A real file where the link belongs is not Speecher's to delete.
        QVERIFY(writeFile(link, "#!/bin/sh\n"));
        removal = removeAppImageIntegration(home.path());
        QCOMPARE(removal.failed.size(), 1);
        QVERIFY(removal.failed.first().startsWith(QStringLiteral("speecher command")));
        QVERIFY(QFile::exists(link));

        QVERIFY(QFile::remove(link));
        const QString renamed = root.filePath(QStringLiteral("dictation"));
        QVERIFY(writeFile(renamed, "image"));
        QVERIFY(QFile::link(renamed, link));
        qputenv("APPIMAGE", QFile::encodeName(renamed));
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.removed.contains(QStringLiteral("speecher command")));
        QVERIFY(!QFileInfo(link).isSymLink());

        qunsetenv("APPIMAGE");
        const QString unrelated = home.filePath(QStringLiteral("SomeoneElse.AppImage"));
        QVERIFY(writeFile(unrelated, "image"));
        QVERIFY(QFile::link(unrelated, link));
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.failed.contains(QStringLiteral(
            "speecher command: the link does not point to a Speecher AppImage")));
        QVERIFY(QFileInfo(link).isSymLink());
    }

    // A custom XDG_DATA_HOME is where this desktop looks for user-installed
    // launchers, and the launcher runs the command link so that moving the
    // AppImage does not break the menu entry.
    void appImageIntegrationFollowsXdgDataHomeAndLaunchesTheCommandLink()
    {
        const QByteArray oldDataHome = qgetenv("XDG_DATA_HOME");
        const bool hadDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        const auto restore = qScopeGuard([&] {
            hadDataHome ? qputenv("XDG_DATA_HOME", oldDataHome) : qunsetenv("XDG_DATA_HOME");
        });

        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString home = root.filePath(QStringLiteral("home"));
        const QString dataHome = root.filePath(QStringLiteral("elsewhere/data"));
        QVERIFY(QDir().mkpath(home));
        qputenv("XDG_DATA_HOME", QFile::encodeName(dataHome));

        const QString appDir = root.filePath(QStringLiteral("mount"));
        const QString binDir = appDir + QStringLiteral("/usr/bin");
        QVERIFY(QDir().mkpath(binDir));
        const auto writeFile = [](const QString &path, const QByteArray &contents) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
        };
        QVERIFY(writeFile(appDir + QStringLiteral("/io.github.firemonster612.speecher.desktop"),
                          "[Desktop Entry]\nExec=speecher\n"
                          "[Desktop Action ToggleDictation]\nExec=speecher toggle\n"));
        QVERIFY(writeFile(appDir + QStringLiteral("/io.github.firemonster612.speecher.svg"), "<svg/>"));
        const QString appImage = root.filePath(QStringLiteral("Speecher.AppImage"));
        QVERIFY(writeFile(appImage, "image"));

        QString error;
        QVERIFY2(installAppImageIntegration(home, appImage, binDir, &error), qPrintable(error));

        const QString desktopFile = QDir(dataHome).filePath(
            QStringLiteral("applications/io.github.firemonster612.speecher.desktop"));
        QVERIFY(QFile::exists(desktopFile));
        QVERIFY(QFile::exists(QDir(dataHome).filePath(QStringLiteral(
            "icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"))));
        QVERIFY(!QFile::exists(QDir(home).filePath(QStringLiteral(".local/share/applications"))));

        const QString link = QDir(home).filePath(QStringLiteral(".local/bin/speecher"));
        QFile installed(desktopFile);
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(),
                 QByteArray("[Desktop Entry]\nExec=\"") + QFile::encodeName(link)
                     + "\"\n[Desktop Action ToggleDictation]\nExec=\"" + QFile::encodeName(link)
                     + "\" toggle\n");

        // Removal looks in the same place it installed to.
        const DesktopIntegrationRemoval removal = removeAppImageIntegration(home);
        QVERIFY(removal.removed.contains(QStringLiteral("app menu entry")));
        QVERIFY(removal.removed.contains(QStringLiteral("app icon")));
        QVERIFY(removal.failed.isEmpty());
        QVERIFY(!QFile::exists(desktopFile));
    }

    void appImageDesktopFileExecLinesUseTheRealImagePath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourcePath = directory.filePath(QStringLiteral("source.desktop"));
        const QString targetPath = directory.filePath(QStringLiteral("installed.desktop"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("[Desktop Entry]\nExec=speecher\n"
                     "[Desktop Action ToggleDictation]\nExec=speecher toggle\n"
                     "[Desktop Action Quoted]\nExec=\"/old Speecher.AppImage\" toggle\n");
        source.close();

        QString error;
        QVERIFY2(writeAppImageDesktopFile(sourcePath,
                                          targetPath,
                                          QStringLiteral("/opt/Speecher Current.AppImage"),
                                          &error),
                 qPrintable(error));
        QFile installed(targetPath);
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(),
                 QByteArray("[Desktop Entry]\n"
                            "Exec=\"/opt/Speecher Current.AppImage\"\n"
                            "[Desktop Action ToggleDictation]\n"
                            "Exec=\"/opt/Speecher Current.AppImage\" toggle\n"
                            "[Desktop Action Quoted]\n"
                            "Exec=\"/opt/Speecher Current.AppImage\" toggle\n"));
    }

    void appImageIntegrationReplacesAStaleCommandLink()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString home = directory.filePath(QStringLiteral("home"));
        const QString appDir = directory.filePath(QStringLiteral("AppDir"));
        const QString binaryDir = QDir(appDir).filePath(QStringLiteral("usr/bin"));
        QVERIFY(QDir().mkpath(binaryDir));

        const QString desktop = QDir(appDir).filePath(
            QStringLiteral("io.github.firemonster612.speecher.desktop"));
        QFile desktopFile(desktop);
        QVERIFY(desktopFile.open(QIODevice::WriteOnly));
        desktopFile.write("[Desktop Entry]\nExec=speecher\n");
        desktopFile.close();

        const QString icon = QDir(appDir).filePath(
            QStringLiteral("io.github.firemonster612.speecher.svg"));
        QFile iconFile(icon);
        QVERIFY(iconFile.open(QIODevice::WriteOnly));
        iconFile.write("<svg/>\n");
        iconFile.close();

        const QString oldImage = directory.filePath(QStringLiteral("old.AppImage"));
        const QString newImage = directory.filePath(QStringLiteral("new.AppImage"));
        for (const QString &path : {oldImage, newImage}) {
            QFile image(path);
            QVERIFY(image.open(QIODevice::WriteOnly));
        }

        const QString commandDir = QDir(home).filePath(QStringLiteral(".local/bin"));
        QVERIFY(QDir().mkpath(commandDir));
        const QString command = QDir(commandDir).filePath(QStringLiteral("speecher"));
        QVERIFY(QFile::link(oldImage, command));

        QString error;
        QVERIFY2(installAppImageIntegration(home, newImage, binaryDir, &error),
                 qPrintable(error));
        QCOMPARE(resolvedPath(QFileInfo(command).symLinkTarget()), resolvedPath(newImage));
        QVERIFY(QFileInfo::exists(QDir(home).filePath(
            QStringLiteral(".local/share/applications/io.github.firemonster612.speecher.desktop"))));
        QVERIFY(QFileInfo::exists(QDir(home).filePath(
            QStringLiteral(".local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"))));
    }

    void relocateAppImageMovesTheImageIntoTheApplicationsFolder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QDir home(root.filePath(QStringLiteral("home")));
        QVERIFY(QDir().mkpath(home.path()));
        const QString image = root.filePath(QStringLiteral("Downloads/Speecher.AppImage"));
        QVERIFY(QDir().mkpath(QFileInfo(image).path()));
        QFile file(image);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("image") == 5);
        file.close();
        QVERIFY(file.setPermissions(file.permissions() | QFileDevice::ExeOwner));

        QString installed;
        QString error;
        QVERIFY2(relocateAppImage(home.path(), image, &installed, &error), qPrintable(error));
        QCOMPARE(installed, resolvedPath(home.filePath(QStringLiteral("Applications/Speecher.AppImage"))));
        QVERIFY(QFileInfo(installed).isFile());
        QVERIFY(QFileInfo(installed).isExecutable());
        QVERIFY(!QFile::exists(image));

        // A second run from the new location is a no-op.
        QString unchanged;
        QVERIFY2(relocateAppImage(home.path(), installed, &unchanged, &error), qPrintable(error));
        QCOMPARE(unchanged, installed);
        QVERIFY(QFileInfo(installed).isFile());
    }

    void relocateAppImagePrefersAnExistingAppImagesFolder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QDir home(root.filePath(QStringLiteral("home")));
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("AppImages"))));

        // A stray file named ~/Applications must not win over a real folder.
        QFile stray(home.filePath(QStringLiteral("Applications")));
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.close();
        QCOMPARE(appImageInstallDirectory(home.path()),
                 home.filePath(QStringLiteral("AppImages")));
        QVERIFY(QFile::remove(home.filePath(QStringLiteral("Applications"))));

        const auto makeImage = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                return false;
            }
            return file.write("image") == 5;
        };

        // No ~/Applications: the existing ~/AppImages folder is the home.
        const QString image = root.filePath(QStringLiteral("Speecher.AppImage"));
        QVERIFY(makeImage(image));
        QString installed;
        QString error;
        QVERIFY2(relocateAppImage(home.path(), image, &installed, &error), qPrintable(error));
        const QString kept = resolvedPath(home.filePath(QStringLiteral("AppImages/Speecher.AppImage")));
        QCOMPARE(installed, kept);

        // An image the user already keeps in ~/AppImages stays there even
        // once ~/Applications exists.
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("Applications"))));
        QVERIFY2(relocateAppImage(home.path(), installed, &installed, &error), qPrintable(error));
        QCOMPARE(installed, kept);

        // With both folders present ~/Applications wins, and a leftover copy
        // of the same name there is replaced.
        const QString elsewhere = root.filePath(QStringLiteral("Other.AppImage"));
        QVERIFY(makeImage(elsewhere));
        QVERIFY(makeImage(home.filePath(QStringLiteral("Applications/Other.AppImage"))));
        QVERIFY2(relocateAppImage(home.path(), elsewhere, &installed, &error), qPrintable(error));
        QCOMPARE(installed, resolvedPath(home.filePath(QStringLiteral("Applications/Other.AppImage"))));
        QVERIFY(!QFile::exists(elsewhere));
    }
#endif

    void failedMediaResumeRetainsOwnershipForRetry()
    {
        using Action = MacMediaController::Action;
        QList<Action> actions;
        QList<QStringList> requestedPlayers;
        MacMediaController::Completion complete;
        MacMediaController media(
            [] { return QStringList{QStringLiteral("player")}; },
            [&](Action action, const QStringList &players, MacMediaController::Completion completion) {
                actions << action;
                requestedPlayers << players;
                complete = std::move(completion);
            });
        media.pausePlaying();
        complete({QStringLiteral("player")});
        media.resumePaused();
        complete({QStringLiteral("player")});
        QCOMPARE(actions.size(), 2); // Failed resume does not spin.
        media.resumePaused();
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions.last(), Action::Resume);
        QCOMPARE(requestedPlayers.last(), QStringList{QStringLiteral("player")});
        complete({});
        media.resumePaused();
        QCOMPARE(actions.size(), 3); // Successful resume relinquishes ownership.
    }

    void mediaOperationsStayOrderedAcrossSessions_data()
    {
        QTest::addColumn<bool>("pauseStillRunning");
        QTest::newRow("late pause") << true;
        QTest::newRow("late resume") << false;
    }

    void mediaOperationsStayOrderedAcrossSessions()
    {
        QFETCH(bool, pauseStillRunning);
        using Action = MacMediaController::Action;
        QList<std::pair<Action, MacMediaController::Completion>> pending;
        bool playing = true;
        MacMediaController media(
            [] { return QStringList{QStringLiteral("player")}; },
            [&](Action action, const QStringList &, MacMediaController::Completion completion) {
                pending.append({action, std::move(completion)});
            });
        const auto complete = [&] {
            auto [action, completion] = pending.takeFirst();
            const bool wasPlaying = playing;
            playing = action == Action::Resume;
            completion(action == Action::Pause && wasPlaying
                           ? QStringList{QStringLiteral("player")} : QStringList{});
        };

        media.pausePlaying();
        if (!pauseStillRunning) complete();
        media.resumePaused();
        media.pausePlaying();
        QCOMPARE(pending.size(), 1);
        complete();
        if (!pending.isEmpty()) complete();
        QVERIFY(!playing);
        QVERIFY(pending.isEmpty());

        media.resumePaused();
        QCOMPARE(pending.size(), 1);
        complete();
        QVERIFY(playing);
        QVERIFY(pending.isEmpty());
    }

    void correctionTrackerSettlesSamplesWithoutRealTimeWaits()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("I use cute every day");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");

        QList<CorrectionEvidence> observed;
        tracker.begin(window, [&observed](const QString &original,
                                          const QString &corrected,
                                          const QString &,
                                          double confidence) {
            observed.append({original, corrected, confidence});
        });
        tracker.sample(QStringLiteral("before text I use cute every day after text"));
        tracker.sample(QStringLiteral("before text I use Qt every day after text"));
        QCOMPARE(observed.size(), 0);
        tracker.sample(QStringLiteral("before text I use Qt every day after text"));

        QCOMPARE(observed.size(), 1);
        QCOMPARE(observed.first().original, QStringLiteral("cute"));
        QCOMPARE(observed.first().corrected, QStringLiteral("Qt"));
        QVERIFY(!tracker.active());
    }

    void correctionTrackerCancelsUnsettledOrUnreadableSamples()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.cancel();
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral(
            "before text Qt after text before text duplicate after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        window.target.selectionStart = 0;
        window.target.selectionEnd = 4;
        window.target.selectedText = QStringLiteral("cute");
        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);
    }

    void correctionTrackerIgnoresEditsThatAreNotCorrections()
    {
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        const auto observed = [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        };

        CorrectionTracker untouched;
        untouched.begin(window, observed);
        untouched.sample(QStringLiteral("before text cute after text"));
        untouched.sample(QStringLiteral("before text cute after text"));
        QCOMPARE(observations, 0);
        QVERIFY(untouched.active());

        CorrectionTracker punctuated;
        punctuated.begin(window, observed);
        punctuated.sample(QStringLiteral("before text cute! after text"));
        punctuated.sample(QStringLiteral("before text cute! after text"));
        QCOMPARE(observations, 0);

        CorrectionTracker rewritten;
        rewritten.begin(window, observed);
        rewritten.sample(QStringLiteral("before text an entirely different phrase after text"));
        rewritten.sample(QStringLiteral("before text an entirely different phrase after text"));
        QCOMPARE(observations, 0);

        // A reading caught mid-edit is no correction, but the watch goes on.
        CorrectionTracker midEdit;
        midEdit.begin(window, observed);
        midEdit.sample(QStringLiteral("before text c after text"));
        midEdit.sample(QStringLiteral("before text Qt after text"));
        midEdit.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 1);
    }

    void correctionTrackerDisablePreventsAndCancelsObservation()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        const auto observed = [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        };

        tracker.setEnabled(false);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.setEnabled(true);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.setEnabled(false);
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.setEnabled(true);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 1);
    }

    void controllerAnswersFromTheInjectedComposition()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);

        QCOMPARE(controller.platform(), platform.get());
        QCOMPARE(controller.outputSummary(), QStringLiteral("Fake: nothing is delivered"));
    }

    void shortcutApiDelegatesToTheCompositionsBinder()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);

        QVERIFY(controller.globalShortcutsSupported());
        QVERIFY(controller.globalShortcut().isEmpty());

        QString error;
        QVERIFY(!controller.setGlobalShortcut(QKeySequence(), &error));
        QCOMPARE(error, QStringLiteral("fake binder rejects an empty sequence"));

        const QKeySequence chosen(Qt::META | Qt::ALT | Qt::Key_D);
        QVERIFY(controller.setGlobalShortcut(chosen));
        QCOMPARE(controller.globalShortcut().combination(), chosen);
    }

    // Delivery injects keystrokes, so both shortcuts must look away for
    // exactly the deliver() call: a single-key binding on an injected key would
    // otherwise take the paste for the user's finger.
    void deliverySuspendsBothShortcutsForExactlyItsDuration()
    {
        FakeGlobalShortcutBinder dictation;
        FakeGlobalShortcutBinder cancel;
        struct ProbingDelivery final : TextDeliveryAdapter {
            FakeGlobalShortcutBinder *dictation = nullptr;
            FakeGlobalShortcutBinder *cancel = nullptr;
            int dictationSuspensions = -1;
            int cancelSuspensions = -1;
            DeliveryResult deliver(const OutputSettings &,
                                   const DeliveryContent &,
                                   const Target &) override
            {
                dictationSuspensions = dictation->suspendCount - dictation->resumeCount;
                cancelSuspensions = cancel->suspendCount - cancel->resumeCount;
                DeliveryResult result;
                result.ok = true;
                return result;
            }
        };
        ProbingDelivery inner;
        inner.dictation = &dictation;
        inner.cancel = &cancel;
        ShortcutSuspendingDelivery delivery(&inner, {&dictation, &cancel});
        QVERIFY(delivery.deliver({}, {}, {}).ok);
        QCOMPARE(inner.dictationSuspensions, 1);
        QCOMPARE(inner.cancelSuspensions, 1);
        QCOMPARE(dictation.resumeCount, 1);
        QCOMPARE(cancel.resumeCount, 1);
    }

#ifdef Q_OS_LINUX
    // The desktop-service binders take combinations only, so each turns a
    // single-key binding away with a reason the UI can show, while
    // combinations pass the per-binding check as before. The watching binders
    // have their own coverage in the keywatch and x11 suites.
    void bindersRefuseASingleKeyWithAReason()
    {
        const ShortcutBinding rightAlt = ShortcutBinding::singleKey(QStringLiteral("AltRight"));
        const ShortcutBinding combo(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        QList<GlobalShortcutBinder *> binders{new KGlobalAccelShortcutBinder(),
                                              new PortalGlobalShortcutBinder()};
        for (GlobalShortcutBinder *binder : binders) {
            const std::unique_ptr<GlobalShortcutBinder> owned(binder);
            QVERIFY(binder->unsupportedBindingReason(combo).isEmpty());
            QVERIFY(!binder->unsupportedBindingReason(rightAlt).isEmpty());
            QString error;
            QVERIFY(!binder->setShortcut(rightAlt, &error));
            QVERIFY(!error.isEmpty());
        }
    }
#endif

    void deferredStartupBindsTheShortcutAndPublishesAccessibility()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QSignalSpy accessibilityChanged(&controller,
                                        &ApplicationController::accessibilityStateChanged);
        QVERIFY(platform->binder);
        QCOMPARE(platform->binder->bindCount, 0);

        controller.frontEndReady();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);

        QCOMPARE(platform->binder->bindCount, 1);
        QVERIFY(controller.accessibilitySupported());
        QVERIFY(controller.accessibilityEnabled());
        QVERIFY(!controller.accessibilityPersistent());

        // The fallback timer must not run the startup a second time.
        controller.frontEndReady();
        QTest::qWait(20);
        QCOMPARE(platform->binder->bindCount, 1);
    }

    void accessibilityChangesRefreshTheControllersCachedState()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->accessibility = {true, false, false};
        ApplicationController controller(true, platform);
        QSignalSpy accessibilityChanged(&controller,
                                        &ApplicationController::accessibilityStateChanged);

        controller.frontEndReady();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);
        QVERIFY(!controller.accessibilityEnabled());
        QVERIFY(platform->accessibilityRefresh);

        platform->accessibility = {true, true, true};
        platform->accessibilityRefresh();

        QVERIFY(controller.accessibilityEnabled());
        QVERIFY(controller.accessibilityPersistent());
        QCOMPARE(accessibilityChanged.count(), 2);
        QCOMPARE(accessibilityChanged.last().at(1).toBool(), true);
    }

    void windowRequestsGoToTheFrontEnd()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->setSetupCompleted(true);

        controller.showMain();
        controller.showSettings();
        controller.showSetup();
        QVERIFY(controller.grabMainWindow(QStringLiteral("/tmp/speecher-grab.png")));

        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("showMainWindow"),
                              QStringLiteral("showSettingsWindow"),
                              QStringLiteral("showSetupAssistant"),
                              QStringLiteral("captureMainWindow /tmp/speecher-grab.png")}));
    }

    void aFinishedModelDownloadIsAnnouncedThroughTheFrontEnd()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        LocalSpeechSettings local = controller.settings()->localSpeechSettings();
        local.modelId = QStringLiteral("moonshine-small");
        controller.settings()->setLocalSpeechSettings(local);
        controller.settings()->setSpeechProvider(QStringLiteral("local"));

        emit controller.localModelStore()->downloadFinished(QStringLiteral("moonshine-small"));

        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("notify Moonshine Small is ready: You can start "
                                             "dictating. Speech stays on this computer.")}));
    }

    void unfinishedSetupSendsTheUserToTheAssistantInstead()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->setSetupCompleted(false);

        controller.showMain();
        controller.showSettings();

        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("showSetupAssistant"),
                              QStringLiteral("showSetupAssistant")}));
    }

    void transcribeCommandOpensTheFilesOnTheFrontEnd()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->setSetupCompleted(true);

        controller.handleIpcCommand(QStringLiteral("transcribe"), {}, nullptr,
                                    {QStringLiteral("/a.wav"), QStringLiteral("/b.mp3")});

        QCOMPARE(frontEnd.calls, QStringList({QStringLiteral("showTranscribeFiles /a.wav /b.mp3")}));
    }

    void filesOpenedBeforeSetupOpenOnceItCompletes()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->setSetupCompleted(false);

        controller.showTranscribeFiles({QStringLiteral("/a.wav")});
        controller.handleIpcCommand(QStringLiteral("transcribe"), {}, nullptr, {QStringLiteral("/b.wav")});
        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("showSetupAssistant"), QStringLiteral("showSetupAssistant")}));

        controller.completeSetup();
        QTRY_COMPARE(frontEnd.calls.size(), 3);
        QCOMPARE(frontEnd.calls.last(), QStringLiteral("showTranscribeFiles /a.wav /b.wav"));
    }
};

int runPlatformCompositionTests(int argc, char **argv)
{
    PlatformCompositionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_platform_composition.moc"
