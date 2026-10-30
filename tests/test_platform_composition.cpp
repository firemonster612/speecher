#include "common/test_suites.h"
#include "common/test_doubles.h"
#include "common/test_manifest_updater_access.h"
#include "common/test_system_singletons.h"

#include "app/AccessibilityMonitor.h"
#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/CommandLine.h"
#include "app/CompositionSockets.h"
#include "app/IpcCommandHandler.h"
#include "app/PlatformComposition.h"
#include "app/ProviderAvailability.h"
#include "app/ProvidersCommand.h"
#include "app/ShortcutSuspendingDelivery.h"
#include "core/LearnedCorrection.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "transcribe/FileTranscriptionSession.h"
#include "transcribe/TranscribeQueue.h"
#include "dictation/DictationSession.h"
#include "platform/CorrectionDiff.h"
#include "platform/mac/MacMediaController.h"
#include "app/LocalSetup.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/HotKeyShortcutBinder.h"
#include "platform/SingleKeyShortcutBinder.h"
#ifdef Q_OS_LINUX
#include "platform/KGlobalAccelShortcutBinder.h"
#include "platform/LinuxDesktopIntegration.h"
#include "platform/PortalGlobalShortcutBinder.h"
#include "transcribe/FileTranscriptionSession.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickAccounts.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickLibrary.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickTranscription.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#endif

#include <QApplication>
#include <QAbstractButton>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPalette>
#include <QPointer>
#include <QProcess>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QSettings>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QtEndian>
#include <QUuid>

#include <QTest>

#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

using namespace speecher;
using namespace speecher::test;

namespace {

// One second of 16 kHz mono s16 silence.
bool writeSilentWav(const QString &path)
{
    const QByteArray data(32000, '\0');
    const auto le32 = [](quint32 v) { QByteArray b(4, 0); qToLittleEndian(v, b.data()); return b; };
    const auto le16 = [](quint16 v) { QByteArray b(2, 0); qToLittleEndian(v, b.data()); return b; };
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write("RIFF" + le32(36 + data.size()) + "WAVEfmt " + le32(16) + le16(1) + le16(1)
                      + le32(16000) + le32(32000) + le16(2) + le16(16) + "data" + le32(data.size()) + data) > 0;
}

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

    QString shortcutDisplay() const override
    {
        return assignedKeys.isEmpty() ? GlobalShortcutBinder::shortcutDisplay() : assignedKeys;
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
        // Announced, as every real binder announces a new binding.
        publishShortcut(shortcut);
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

    // The keys a desktop that picks them itself (the portal) says it
    // assigned; shortcut() stays whatever stand-in the binder keeps.
    void publishAssignedKeys(const QString &keys)
    {
        assignedKeys = keys;
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
    QString assignedKeys;

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

// The macOS and Windows hot-key bookkeeping over a registration that only
// remembers which keys it holds.
class FakeHotKeyShortcutBinder final : public HotKeyShortcutBinder {
public:
    using HotKeyShortcutBinder::HotKeyShortcutBinder;

    QKeySequence registeredKeys;

protected:
    bool registerKeys(const QKeySequence &keys, QString *) override
    {
        registeredKeys = keys;
        return true;
    }
    void unregisterKeys() override { registeredKeys = {}; }
    bool registered() const override { return !registeredKeys.isEmpty(); }
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
        return ipcName.isEmpty() ? m_delegate->ipcListenName() : ipcName;
    }

    QStringList ipcConnectCandidates() const override
    {
        return ipcName.isEmpty() ? m_delegate->ipcConnectCandidates() : QStringList{ipcName};
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
        return audioInputs ? audioInputs(parent) : m_delegate->createAudioInput(settings, parent);
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
        if (fakeTarget) {
            target = new FakeTargetProvider(parent);
            return target;
        }
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

    GlobalShortcutBinder *createGlobalShortcutBinder(GlobalShortcutRole role,
                                                     SettingsStore *,
                                                     QObject *parent) const override
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

    std::optional<float> inputVolume() const override
    {
        return volume;
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
        ++launchAtLoginRequests;
        if (launchAtLoginError.isEmpty()) {
            return true;
        }
        if (error) {
            *error = launchAtLoginError;
        }
        return false;
    }

    std::optional<float> volume;
    mutable QString launchAtLoginError;
    mutable int launchAtLoginRequests = 0;
    // Set, the app listens here instead of where the running Speecher does.
    QString ipcName;
    // A fake Target, set before the controller is built, for tests that edit
    // a selection.
    bool fakeTarget = false;
    mutable FakeTargetProvider *target = nullptr;

    mutable std::function<void(bool)> microphoneAnswer;
    // Makes the microphones instead of the platform, when set.
    std::function<AudioInput *(QObject *parent)> audioInputs;
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
// A connection to the controller's own IPC, as a command line would make.
static void connectToController(QLocalSocket &socket, const QString &ipcName)
{
    socket.connectToServer(ipcName);
    QVERIFY(socket.waitForConnected(2000));
}

// Every line socket has been written once the app has had a moment to write
// more.
static QList<QJsonObject> linesWritten(QLocalSocket &socket)
{
    QTest::qWait(200);
    QList<QJsonObject> lines;
    while (socket.canReadLine()) {
        lines << QJsonDocument::fromJson(socket.readLine()).object();
    }
    return lines;
}

static QString uniqueControllerIpcName()
{
    return QStringLiteral("spchr-c-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
}

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

// What speecher prints on stdout for the arguments after its name.
QString printedBy(const QStringList &arguments, int *exitCode = nullptr)
{
    std::ostringstream out;
    std::streambuf *const stdoutBuffer = std::cout.rdbuf(out.rdbuf());
    const auto restoreStdout = qScopeGuard([stdoutBuffer] { std::cout.rdbuf(stdoutBuffer); });
    const int status = parseCommandLine(QStringList{QStringLiteral("speecher")} + arguments, {}).exitCode;
    if (exitCode) {
        *exitCode = status;
    }
    return QString::fromStdString(out.str());
}

// What bash's completion function in script offers for words, the first the
// command and the last the word being completed, run in dir, which is also
// HOME, after prelude.
// line is the command line bash split into words, by default the words
// between spaces.
QStringList bashCompletions(const QString &script, const QStringList &words, const QString &dir = {},
                            const QString &line = {}, const QString &prelude = {})
{
    QProcess bash;
    bash.setWorkingDirectory(dir);
    if (!dir.isEmpty()) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("HOME"), dir);
        bash.setProcessEnvironment(environment);
    }
    bash.start(QStringLiteral("bash"),
               QStringList{QStringLiteral("--norc"), QStringLiteral("--noprofile"), QStringLiteral("-c"),
                           prelude + QLatin1Char('\n') + script
                               + QStringLiteral("COMP_LINE=$1; COMP_POINT=${#COMP_LINE}; shift; "
                                                "COMP_WORDS=(\"$@\"); COMP_CWORD=$(($# - 1)); _speecher; "
                                                "printf '%s\\n' \"${COMPREPLY[@]}\""),
                           QStringLiteral("bash"), line.isEmpty() ? words.join(QLatin1Char(' ')) : line}
                   + words);
    if (!bash.waitForFinished() || bash.exitCode() != 0) {
        qWarning().noquote() << "bash failed:" << bash.readAllStandardError();
        return {QStringLiteral("<bash failed>")};
    }
    return QString::fromUtf8(bash.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// What zsh's completion function in script offers for words, as
// bashCompletions has them, with compadd and _files printing what they are
// given in place of zsh's completion system.
QStringList zshCompletions(const QString &script, const QStringList &words)
{
    QProcess zsh;
    zsh.start(QStringLiteral("zsh"),
              QStringList{QStringLiteral("-f"), QStringLiteral("-c"),
                          QStringLiteral("compdef() { :; }; _files() { print -r -- '<files>'; }; "
                                         "compadd() { while [[ $1 != -- ]]; do shift; done; shift; "
                                         "print -rl -- \"$@\"; }\n")
                              + script
                              + QStringLiteral("words=(\"$@\"); CURRENT=$#; PREFIX=${words[CURRENT]}; _speecher"),
                          QStringLiteral("zsh")}
                  + words);
    if (!zsh.waitForFinished() || zsh.exitCode() != 0) {
        qWarning().noquote() << "zsh failed:" << zsh.readAllStandardError();
        return {QStringLiteral("<zsh failed>")};
    }
    return QString::fromUtf8(zsh.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// Writes a speecher into dir that prints listed whatever it is asked, as
// `speecher completions --list` would, and returns its path, or nothing when
// it cannot.
QString fakeSpeecher(const QString &dir, const QString &listed)
{
    const QString path = QDir(dir).filePath(QStringLiteral("speecher"));
    QFile fake(path);
    if (!fake.open(QIODevice::WriteOnly)) {
        return {};
    }
    fake.write("#!/bin/sh\ncat <<'EOF'\n" + listed.toUtf8() + "EOF\n");
    fake.close();
    return fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner) ? path : QString();
}

#ifdef Q_OS_LINUX
// A shown window holding the SchemaRow for rowId, or null with the reason in
// the log.
std::unique_ptr<QQuickWindow> schemaRowWindow(QQmlEngine &engine, const QString &rowId)
{
    QQmlComponent component(&engine);
    component.setData(QStringLiteral(R"(
        import QtQuick
        import Speecher.App
        Window {
            width: 640; height: 160; visible: true
            SchemaRow { rowId: "%1"; width: parent.width }
        }
    )").arg(rowId).toUtf8(), QUrl());
    std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
    if (!window || !QTest::qWaitForWindowExposed(window.get())) {
        qWarning().noquote() << "no SchemaRow window:" << component.errorString();
        return nullptr;
    }
    return window;
}

// The item in window whose QML type is typeName, or null.
QQuickItem *findItem(QQuickWindow *window, const char *typeName)
{
    for (QQuickItem *item : window->findChildren<QQuickItem *>()) {
        if (QByteArray(item->metaObject()->className()).startsWith(typeName)) {
            return item;
        }
    }
    return nullptr;
}

// Clicks the ShortcutField in window so it listens, as a person starts recording.
void startListening(QQuickWindow *window, QQuickItem *field)
{
    QTest::mouseClick(window, Qt::LeftButton, {}, field->mapToScene(field->boundingRect().center()).toPoint());
    QTRY_VERIFY(field->property("listening").toBool());
}
#endif

#ifdef Q_OS_LINUX
// Writes contents to path, making its folder first.
bool writeFixtureFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).path()) && file.open(QIODevice::WriteOnly)
        && file.write(contents) == contents.size();
}
#endif

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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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

    // While a selection edit waits for review, holding the Global Shortcut in
    // push-to-talk mode dictates a follow-up once the hold delay passes.
    void pushToTalkDictatesAFollowUpToAReview()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->fakeTarget = true;
        platform->audioInputs = [](QObject *parent) { return new FakeAudioInput(parent); };
        ApplicationController controller(true, platform);
        SettingsStore *settings = controller.settings();
        const bool setupCompleted = settings->dictationSnapshot().setupCompleted;
        const ShortcutActivationMode mode = settings->dictationSnapshot().shortcutActivationMode;
        const QString refinementProvider = settings->dictationSnapshot().refinement.providerId;
        const auto restore = qScopeGuard([&] {
            settings->edit([&](AppSettings &draft) {
                draft.setupCompleted = setupCompleted;
                draft.shortcutActivationMode = mode;
                draft.refinement.providerId = refinementProvider;
                return true;
            });
        });
        settings->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.shortcutActivationMode = ShortcutActivationMode::PushToTalk;
            draft.refinement.providerId = QStringLiteral("openai");
            return true;
        });
        FakeSpeechTranscriber *speech = nullptr;
        FakeRefiner *refiner = nullptr;
        registerFakeSpeechProvider(*controller.providerRegistry(), &speech);
        registerFakeRefiner(*controller.providerRegistry(), &refiner);
        platform->target->target.applicationId = QStringLiteral("org.kde.kate");
        platform->target->target.selectedText = QStringLiteral("Wednesday");
        platform->target->target.selectionStart = 0;
        platform->target->target.selectionEnd = 9;
        DictationSession *session = controller.session();
        session->startListening();
        QTRY_COMPARE(session->state(), DictationState::Listening);
        speech->emitFinalText(QStringLiteral("make it Thursday"));
        session->stopListening();
        QTRY_COMPARE(refiner->refineCalls, 1);
        refiner->emitCompletedText(QStringLiteral("Thursday"));
        QCOMPARE(session->state(), DictationState::Reviewing);

        emit platform->binder->activated();
        QTRY_VERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QTRY_COMPARE(session->state(), DictationState::Listening);
        // Released with nothing said, the follow-up leaves the review as it was.
        emit platform->binder->deactivated();
        QTRY_COMPARE(session->state(), DictationState::Reviewing);
        controller.cancel();
        QCOMPARE(session->state(), DictationState::Idle);
    }

#ifdef Q_OS_MACOS
    // Each dictation logs the macOS default input volume once it listens.
    void listeningLogsTheInputVolume()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->audioInputs = [](QObject *parent) { return new FakeAudioInput(parent); };
        platform->volume = 0.4f;
        ApplicationController controller(true, platform);
        SettingsStore *settings = controller.settings();
        const bool setupCompleted = settings->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            settings->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        settings->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        FakeSpeechTranscriber *speech = nullptr;
        registerFakeSpeechProvider(*controller.providerRegistry(), &speech);
        QTest::ignoreMessage(QtInfoMsg, "macOS default input volume=40%");
        controller.session()->startListening();
        QTRY_COMPARE(controller.session()->state(), DictationState::Listening);
        controller.cancel();
    }
#endif

    void dictationAndFileTranscriptionExcludeEachOther()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("silence.wav"));
        QVERIFY(writeSilentWav(audio));
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        TranscribeQueue queue(controller.settings(), controller.providerRegistry(), controller.fileTranscription(),
                              [&controller] { return controller.fileTranscriptionRefusal(); });
        queue.addFiles({audio});
        queue.setOptions(options);

        // A batch in flight turns the dictation shortcut away.
        QVERIFY(queue.start());
        controller.toggle();
        QVERIFY(!platform->microphoneAnswer);
        controller.fileTranscription()->cancel();

        // A dictation waiting on the microphone turns a batch away.
        controller.toggle();
        QVERIFY(platform->microphoneAnswer);
        QVERIFY(!queue.start());
        QVERIFY(!queue.problem().isEmpty());
        controller.stopListening();
    }

    // A fresh profile on a daemon with no front end has no setup assistant to
    // finish, so dictation and recording go on to ask for the microphone.
    void freshProfileWithoutFrontEndSkipsSetup()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = false; return true; });

        controller.startListening();
        QVERIFY(platform->microphoneAnswer);
        controller.stopListening();

        platform->microphoneAnswer = nullptr;
        QTemporaryDir dir;
        IpcRequest recordStart{IpcCommand::RecordStart, {}, {dir.filePath(QStringLiteral("take.wav"))}};
        recordStart.microphoneOnly = true;
        controller.ipcCommands()->handle(recordStart, nullptr);
        QVERIFY(platform->microphoneAnswer);
    }

    void shortcutReleasePreservesRecordingError()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const ShortcutActivationMode mode = controller.settings()->dictationSnapshot().shortcutActivationMode;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) {
                draft.setupCompleted = setupCompleted;
                draft.shortcutActivationMode = mode;
                return true;
            });
        });
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.shortcutActivationMode = ShortcutActivationMode::Hybrid;
            return true;
        });

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
        SettingsStore settings;
        FakeSingleKeyShortcutBinder binder(&settings, GlobalShortcutBinder::actionFor(GlobalShortcutRole::Dictation));
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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });

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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const ShortcutActivationMode mode = controller.settings()->dictationSnapshot().shortcutActivationMode;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) {
                draft.setupCompleted = setupCompleted;
                draft.shortcutActivationMode = mode;
                return true;
            });
        });
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.shortcutActivationMode = ShortcutActivationMode::PushToTalk;
            return true;
        });

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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const ShortcutActivationMode mode = controller.settings()->dictationSnapshot().shortcutActivationMode;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) {
                draft.setupCompleted = setupCompleted;
                draft.shortcutActivationMode = mode;
                return true;
            });
        });
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.shortcutActivationMode = ShortcutActivationMode::Toggle;
            return true;
        });

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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const ShortcutActivationMode mode = controller.settings()->dictationSnapshot().shortcutActivationMode;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) {
                draft.setupCompleted = setupCompleted;
                draft.shortcutActivationMode = mode;
                return true;
            });
        });
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.shortcutActivationMode = ShortcutActivationMode::PushToTalk;
            return true;
        });

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

#ifdef Q_OS_LINUX
    // Recording on the Shortcuts page's row hands the keys to the controller,
    // and setup's shortcut step then counts the shortcut as set.
    void aShortcutRecordedInTheUiIsBound()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        // Setup's choices read Library.
        const QuickLibrary::ControllerScope library(&controller);
        QuickSettings settings(&controller);
        // The step's check is System's.
        test::SystemSingletons system(&controller);
        // Setup's choices read Models, as the host always has one.
        QuickModels models(&controller);
        // Setup reads Accounts, which the host always makes. Its keyring
        // reads fail at once, so none is left waiting on this thread when it
        // goes.
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        QuickAccounts accounts(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const std::unique_ptr<QQuickWindow> window = schemaRowWindow(engine, QStringLiteral("globalShortcut"));
        QVERIFY(window);
        QQmlComponent choicesComponent(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/setup/SetupChoices.qml")));
        std::unique_ptr<QObject> choices(choicesComponent.create());
        QVERIFY2(choices, qPrintable(choicesComponent.errorString()));

        QQuickItem *field = findItem(window.get(), "ShortcutField");
        QVERIFY(field);
        const auto record = [&] {
            startListening(window.get(), field);
            QTest::keyClick(window.get(), Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        };
        const auto shortcutStep = [&] {
            for (const QVariant &step : choices->property("steps").toList()) {
                if (step.toMap().value(QStringLiteral("id")) == QStringLiteral("shortcut")) {
                    return step.toMap();
                }
            }
            return QVariantMap();
        };
        QVERIFY(!shortcutStep().value(QStringLiteral("done")).toBool());

        record();
        QCOMPARE(controller.globalShortcut(), ShortcutBinding(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D)));
        QCOMPARE(settings.values().value(QStringLiteral("globalShortcut")).toStringList(),
                 (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("D")}));
        QTRY_VERIFY(shortcutStep().value(QStringLiteral("done")).toBool());
    }

    // Where the desktop picks the keys itself (the portal), clicking the row
    // opens its chooser. The row never listens, so no shortcut lets go of its
    // keys and pressing them records nothing.
    void aDesktopThatPicksTheKeysOpensItsChooserFromTheRow()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        platform->binder->desktopChooser = true;
        QuickSettings settings(&controller);
        QVERIFY(controller.setGlobalShortcut(QKeySequence(Qt::CTRL | Qt::Key_K)));
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const std::unique_ptr<QQuickWindow> window = schemaRowWindow(engine, QStringLiteral("globalShortcut"));
        QVERIFY(window);
        QQuickItem *field = findItem(window.get(), "ShortcutField");
        QVERIFY(field);

        QTest::mouseClick(window.get(), Qt::LeftButton, {}, field->mapToScene(field->boundingRect().center()).toPoint());
        QCOMPARE(platform->binder->registerCount, 1);
        QVERIFY(!field->property("listening").toBool());
        QTest::keyClick(window.get(), Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(platform->binder->suspendCount, 0);
        QCOMPARE(controller.globalShortcut(), ShortcutBinding(QKeySequence(Qt::CTRL | Qt::Key_K)));
    }

    // While a row records, every Global Shortcut lets go of its keys, so the
    // bound ones record rather than start dictation. They come back however
    // recording ends: a recorded shortcut, Escape, focus moving away, the
    // window hiding (as closing it does), or the page closing.
    void recordingInTheUiSuspendsTheShortcuts()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QuickSettings settings(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        std::unique_ptr<QQuickWindow> window = schemaRowWindow(engine, QStringLiteral("globalShortcut"));
        QVERIFY(window);
        QQuickItem *field = findItem(window.get(), "ShortcutField");
        QVERIFY(field);
        FakeGlobalShortcutBinder *binder = platform->binder;

        startListening(window.get(), field);
        QCOMPARE(binder->suspendCount, 1);
        QCOMPARE(platform->cancelBinder->suspendCount, 1);
        QCOMPARE(platform->pauseBinder->suspendCount, 1);
        QCOMPARE(binder->resumeCount, 0);
        QTest::keyClick(window.get(), Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(binder->resumeCount, 1);
        QCOMPARE(platform->cancelBinder->resumeCount, 1);

        startListening(window.get(), field);
        QTest::keyClick(window.get(), Qt::Key_Escape);
        QVERIFY(!field->property("listening").toBool());
        QCOMPARE(binder->resumeCount, 2);

        startListening(window.get(), field);
        field->setFocus(false);
        QVERIFY(!field->property("listening").toBool());
        QCOMPARE(binder->resumeCount, 3);

        startListening(window.get(), field);
        window->hide();
        QVERIFY(!field->property("listening").toBool());
        QCOMPARE(binder->resumeCount, 4);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));

        startListening(window.get(), field);
        window.reset();
        QCOMPARE(binder->suspendCount, 5);
        QCOMPARE(binder->resumeCount, 5);
    }

    // A shortcut the controller or the desktop's chooser refuses says why in
    // its row. The next recording starts without the old reason, as do keys
    // changed elsewhere, such as from the CLI.
    void aRefusedShortcutSaysWhyInItsRow()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QuickSettings settings(&controller);
        QuickNavigation navigation(nullptr);
        QQmlEngine engine;
        const std::unique_ptr<QQuickWindow> window = schemaRowWindow(engine, QStringLiteral("globalShortcut"));
        QVERIFY(window);
        QQuickItem *row = findItem(window.get(), "SchemaRow");
        QQuickItem *field = findItem(window.get(), "ShortcutField");
        QVERIFY(row && field);
        const QString help = row->property("help").toString();
        const QString owned = QStringLiteral("Another application already owns Ctrl+Alt+D");
        platform->binder->setShortcutError = owned;

        startListening(window.get(), field);
        QTest::keyClick(window.get(), Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(row->property("help").toString(), owned);

        startListening(window.get(), field);
        QCOMPARE(row->property("help").toString(), help);

        const QString refused = QStringLiteral("The desktop refused the shortcut");
        platform->binder->publishRegistrationResult(false, refused);
        QCOMPARE(row->property("help").toString(), refused);
        platform->binder->setShortcutError.clear();
        QVERIFY(controller.setGlobalShortcut(QKeySequence(Qt::CTRL | Qt::Key_K)));
        QCOMPARE(row->property("help").toString(), help);
    }

    // Where the desktop picks the keys itself (the portal), the binding is a
    // stand-in: the rows show the keys the desktop says it
    // assigned, and follow when it assigns others.
    void theUiShowsTheKeysTheDesktopAssigned()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QuickSettings settings(&controller);
        QVERIFY(controller.setGlobalShortcut(GlobalShortcutBinder::defaultShortcut()));
        const auto shown = [&] {
            return settings.values().value(QStringLiteral("globalShortcut")).toStringList();
        };

        platform->binder->publishAssignedKeys(QStringLiteral("Ctrl+Space"));
        QCOMPARE(shown(), (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Space")}));

        QSignalSpy changed(&settings, &QuickSettings::valuesChanged);
        platform->binder->publishAssignedKeys(QStringLiteral("Ctrl+Alt+K"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(shown(), (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("K")}));
    }
#endif

    // The Cancel and Pause Shortcuts hold their keys only while a Dictation
    // Session can use them, so a bare key types normally the rest of the time.
    void sessionShortcutsAreArmedOnlyDuringASession()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        const bool setupCompleted = controller.settings()->dictationSnapshot().setupCompleted;
        const auto restore = qScopeGuard([&] {
            controller.settings()->edit([&](AppSettings &draft) { draft.setupCompleted = setupCompleted; return true; });
        });
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
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
        SettingsStore settings;
        for (const GlobalShortcutRole role : {GlobalShortcutRole::Cancel, GlobalShortcutRole::Pause}) {
            FakeSingleKeyShortcutBinder session(&settings, GlobalShortcutBinder::actionFor(role));
            QCOMPARE(session.unsupportedBindingReason(c),
                     QStringLiteral("Speecher can only watch %1, not take it from other apps, so it would "
                                    "still type while you dictate. Use another key or a key combination.")
                         .arg(c.displayText()));
            QVERIFY(session.unsupportedBindingReason(f13).isEmpty());
        }
        FakeSingleKeyShortcutBinder dictation(&settings, GlobalShortcutBinder::actionFor(GlobalShortcutRole::Dictation));
        QVERIFY(dictation.unsupportedBindingReason(c).isEmpty());
    }

    // The Cancel Shortcut keeps its binding under its own key, next to the
    // dictation shortcut's, and a new binder reads it back.
    void cancelShortcutPersistsUnderItsOwnKey()
    {
        SettingsStore settings;
        settings.raw().remove(QStringLiteral("shortcuts/toggleDictation"));
        const GlobalShortcutAction cancel = GlobalShortcutBinder::actionFor(GlobalShortcutRole::Cancel);
        const ShortcutBinding f13 = ShortcutBinding::singleKey(QStringLiteral("F13"));

        FakeSingleKeyShortcutBinder binder(&settings, cancel);
        QVERIFY(binder.setShortcut(f13));
        QCOMPARE(settings.raw().value(QStringLiteral("shortcuts/cancelDictation")).toString(), QStringLiteral("key:F13"));
        QVERIFY(!settings.raw().contains(QStringLiteral("shortcuts/toggleDictation")));

        FakeSingleKeyShortcutBinder restored(&settings, cancel);
        restored.bind();
        QCOMPARE(restored.shortcut(), f13);
        QVERIFY(restored.setShortcut({}));
        QVERIFY(!settings.raw().contains(QStringLiteral("shortcuts/cancelDictation")));
    }

    // On macOS and Windows the Dictation shortcut lives in settings. One never
    // set binds the default; one the person cleared stays cleared at the next
    // launch instead of coming back as the default.
    void clearedHotKeyStaysClearedAfterRestart()
    {
        SettingsStore settings;
        settings.raw().remove(QStringLiteral("shortcuts/toggleDictation"));
        const auto forget = qScopeGuard([&] { settings.raw().remove(QStringLiteral("shortcuts/toggleDictation")); });
        const GlobalShortcutAction dictation = GlobalShortcutBinder::actionFor(GlobalShortcutRole::Dictation);

        FakeHotKeyShortcutBinder first(&settings, dictation);
        first.bind();
        QCOMPARE(first.registeredKeys, GlobalShortcutBinder::defaultShortcut());
        QVERIFY(first.setShortcut({}));
        QVERIFY(first.registeredKeys.isEmpty());

        FakeHotKeyShortcutBinder relaunched(&settings, dictation);
        relaunched.bind();
        QVERIFY(relaunched.shortcut().isEmpty());
        QVERIFY(relaunched.registeredKeys.isEmpty());
    }

    // An isolated instance has settings and a socket of its own, and leaves
    // the person's login item alone.
    void anIsolatedInstanceKeepsApartFromThePersonsOwn()
    {
        const QString personsSocket = appSocketName();
        qputenv("SPEECHER_INSTANCE", "verify");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_INSTANCE"); });
        QCOMPARE(SettingsKeys::application(), QStringLiteral("speecher-verify"));
        QCOMPARE(appSocketName(), QStringLiteral("speecher-verify") + personsSocket.mid(QStringLiteral("speecher").size()));

        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        controller.settings()->edit([](AppSettings &draft) { draft.launchAtLogin = true; return true; });
        QCOMPARE(platform->launchAtLoginRequests, 0);
    }

    // A fresh profile opens setup on a plain launch, but --daemon shows no
    // window by itself; the daemon a CLI command starts for setup asks for it.
    void onlyALaunchWithAWindowOpensPendingSetup()
    {
        const auto launch = [](const QStringList &arguments) {
            return parseCommandLine(QStringList{QStringLiteral("speecher")} + arguments, {});
        };
        QVERIFY(opensSetupAtLaunch(launch({}), true));
        QVERIFY(!opensSetupAtLaunch(launch({}), false));
        QVERIFY(!opensSetupAtLaunch(launch({QStringLiteral("--daemon")}), true));
        QVERIFY(opensSetupAtLaunch(launch({QStringLiteral("--daemon"), QStringLiteral("--show-setup")}), false));
    }

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    void guiLaunchKeepsRunningAfterLastWindowCloses()
    {
        QVERIFY(!quitOnLastWindowClosed(LaunchMode::RunGui));
        QVERIFY(!quitOnLastWindowClosed(LaunchMode::RunDaemon));
    }

    void anUpdateRelaunchDropsStartupActions()
    {
        QCOMPARE(updateRelaunchArguments({QStringLiteral("--daemon"),
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
        QCOMPARE(updateRelaunchArguments({QStringLiteral("transcribe"), QStringLiteral("memo.wav")}),
                 QStringList());
        QCOMPARE(updateRelaunchArguments({QStringLiteral("memo.wav")}), QStringList());
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
        QCOMPARE(decision.ipcCommand, IpcCommand::Cancel);
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

        // - reads stdin, always headless, saving nowhere unless given a folder.
        const auto parseStdin = [](QStringList options) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("transcribe")} + options, {});
        };
        const CommandLineDecision piped = parseStdin({QStringLiteral("--stdout"), QStringLiteral("-")});
        QCOMPARE(piped.mode, LaunchMode::TranscribeHeadless);
        QCOMPARE(piped.transcribeFiles, QStringList{QStringLiteral("-")});
        QCOMPARE(piped.headless.destination, TranscriptDestination::None);
        QCOMPARE(parseStdin({QStringLiteral("--output"), dir.path(), QStringLiteral("-")}).headless.destination,
                 TranscriptDestination::Folder);
        for (const QStringList &mistake : {QStringList{QStringLiteral("-")},
                                           QStringList{QStringLiteral("--headless"), QStringLiteral("-")},
                                           QStringList{QStringLiteral("--stdout"), QStringLiteral("--output"),
                                                       QStringLiteral("beside"), QStringLiteral("-")},
                                           QStringList{QStringLiteral("--output"), QStringLiteral("none"),
                                                       QStringLiteral("-")},
                                           QStringList{QStringLiteral("--stdout"), QStringLiteral("-"), audio}}) {
            QCOMPARE(parseStdin(mistake).exitCode, 2);
        }

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

    // providers takes --json and nothing else. Its table and its JSON say
    // unknown, or null, for what nobody has checked.
    void providersPrintsATableOrJson()
    {
        const auto parse = [](QStringList options) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("providers")} + options,
                                    {});
        };
        QCOMPARE(parse({}).mode, LaunchMode::ListProviders);
        QVERIFY(!parse({}).json);
        QVERIFY(parse({QStringLiteral("--json")}).json);
        {
            std::ostringstream usage;
            std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
            const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
            QCOMPARE(parse({QStringLiteral("--table")}).exitCode, 2);
            QVERIFY(QString::fromStdString(usage.str()).startsWith(QStringLiteral("Unknown providers option: --table\n")));
        }

        const QList<ProviderReport> reports{
            {QStringLiteral("codex"), ProviderRole::Speech, QStringLiteral("ChatGPT Codex"), true, true},
            {QStringLiteral("local"), ProviderRole::Refinement, QStringLiteral("Local Runner"), false, true,
             std::nullopt, false, QStringLiteral("Ollama isn't running.")},
        };
        std::ostringstream json;
        printProviderReports(reports, true, json);
        // The apostrophe stays out of the raw strings: moc reads one there as
        // a character literal and loses the slots that follow.
        QCOMPARE(QString::fromStdString(json.str()),
                 QStringLiteral(R"([{"configured":true,"id":"codex","label":"ChatGPT Codex","problem":null,)"
                                R"("role":"speech","signedIn":null,"signsIn":true,"usable":null},)"
                                R"({"configured":true,"id":"local","label":"Local Runner",)"
                                "\"problem\":\"Ollama isn't running.\","
                                R"("role":"refinement","signedIn":null,)"
                                R"("signsIn":false,"usable":false}])"
                                "\n"));
        std::ostringstream table;
        printProviderReports(reports, false, table);
        QCOMPARE(QString::fromStdString(table.str()),
                 QStringLiteral("Role        ID     Name           Configured  Signed in  Usable   Problem\n"
                                "Speech      codex  ChatGPT Codex  Yes         Unknown    Unknown\n"
                                "Refinement  local  Local Runner   Yes         -          No       "
                                "Ollama isn't running.\n"));
    }

    // record's subcommands go to the running Speecher; --to is made absolute
    // and --vocab-file read here, where the shell's directory is known.
    void recordIsSentToTheRunningSpeecher()
    {
        const auto parse = [](QStringList arguments) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("record")} + arguments, {});
        };

        const CommandLineDecision start =
            parse({QStringLiteral("start"), QStringLiteral("--to"), QStringLiteral("call.md"), QStringLiteral("--mic-only")});
        QCOMPARE(start.mode, LaunchMode::RunCli);
        QCOMPARE(start.ipcCommand, IpcCommand::RecordStart);
        QCOMPARE(start.recordPath, QDir::current().absoluteFilePath(QStringLiteral("call.md")));
        QVERIFY(start.recordMicrophoneOnly);
        QCOMPARE(parse({QStringLiteral("start")}).recordPath, QString());
        QVERIFY(!parse({QStringLiteral("start")}).recordMicrophoneOnly);
        QTemporaryDir dir;
        const QString vocabulary = dir.filePath(QStringLiteral("terms.txt"));
        QFile vocabularyFile(vocabulary);
        QVERIFY(vocabularyFile.open(QIODevice::WriteOnly));
        vocabularyFile.write("readSharedChoice\n# a comment\nSpeecher CLI\n");
        vocabularyFile.close();
        QCOMPARE(parse({QStringLiteral("start"), QStringLiteral("--vocab-file"), vocabulary}).vocabularyTerms,
                 QStringList({QStringLiteral("readSharedChoice"), QStringLiteral("Speecher CLI")}));
        const CommandLineDecision status = parse({QStringLiteral("status"), QStringLiteral("--json")});
        QCOMPARE(status.ipcCommand, IpcCommand::RecordStatus);
        QVERIFY(status.json);
        QCOMPARE(parse({QStringLiteral("stop")}).ipcCommand, IpcCommand::RecordStop);

        std::ostringstream usage;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        for (const QStringList &mistake : {QStringList{},
                                           QStringList{QStringLiteral("pause")},
                                           QStringList{QStringLiteral("start"), QStringLiteral("--to")},
                                           QStringList{QStringLiteral("start"), QStringLiteral("--to"), QDir::tempPath()},
                                           QStringList{QStringLiteral("start"), QStringLiteral("--json")},
                                           QStringList{QStringLiteral("start"), QStringLiteral("--vocab-file")},
                                           QStringList{QStringLiteral("start"), QStringLiteral("--vocab-file"),
                                                       dir.filePath(QStringLiteral("missing.txt"))},
                                           QStringList{QStringLiteral("status"), QStringLiteral("--vocab-file"), vocabulary},
                                           QStringList{QStringLiteral("stop"), QStringLiteral("--to"), QStringLiteral("a.md")},
                                           QStringList{QStringLiteral("status"), QStringLiteral("--profile"),
                                                       QStringLiteral("ai-coding")}}) {
            const CommandLineDecision refused = parse(mistake);
            QCOMPARE(refused.mode, LaunchMode::Exit);
            QCOMPARE(refused.exitCode, 2);
        }
    }

    // status --watch keeps asking the running Speecher; --json goes with
    // either.
    void statusTakesJsonAndWatch()
    {
        const auto parse = [](QStringList options) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("status")} + options, {});
        };
        QCOMPARE(parse({}).ipcCommand, IpcCommand::Status);
        QVERIFY(!parse({}).json);
        QVERIFY(parse({QStringLiteral("--json")}).json);
        const CommandLineDecision watch = parse({QStringLiteral("--watch"), QStringLiteral("--json")});
        QCOMPARE(watch.mode, LaunchMode::RunCli);
        QCOMPARE(watch.ipcCommand, IpcCommand::WatchStatus);
        QVERIFY(watch.json);

        std::ostringstream usage;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        QCOMPARE(parse({QStringLiteral("--follow")}).exitCode, 2);
        QVERIFY(QString::fromStdString(usage.str()).startsWith(QStringLiteral("Unknown status option: --follow\n")));
    }

    // Every usage mistake prints its message, then --help, and exits 2. An
    // option given last, without its value, says so the same way everywhere.
    void usageMistakesPrintTheMessageThenHelp()
    {
        std::ostringstream usage;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        const QList<std::pair<QStringList, QString>> mistakes{
            {{QStringLiteral("start"), QStringLiteral("--format")}, QStringLiteral("--format requires a value")},
            {{QStringLiteral("listen"), QStringLiteral("--tone")}, QStringLiteral("--tone requires a value")},
            {{QStringLiteral("transcribe"), QStringLiteral("--output")}, QStringLiteral("--output requires a value")},
            {{QStringLiteral("record"), QStringLiteral("start"), QStringLiteral("--to")},
             QStringLiteral("--to requires a value")},
            {{QStringLiteral("status"), QStringLiteral("--format"), QStringLiteral("html")},
             QStringLiteral("--format can only be used with toggle or start")},
        };
        for (const auto &[arguments, message] : mistakes) {
            usage.str({});
            QCOMPARE(parseCommandLine(QStringList{QStringLiteral("speecher")} + arguments, {}).exitCode, 2);
            QVERIFY2(QString::fromStdString(usage.str()).startsWith(message + QStringLiteral("\n\nUsage: speecher")),
                     usage.str().c_str());
        }
    }

    // Each shell's script offers every command and subcommand --help
    // documents where it is typed, and each command's options after it.
    void completionsCoverEveryCommandInHelp()
    {
        const QStringList dictation{QStringLiteral("--format"), QStringLiteral("--profile"),
                                    QStringLiteral("--language")};
        const QStringList shared{QStringLiteral("--model"),   QStringLiteral("--no-vocabulary"),
                                 QStringLiteral("--vocab-file"), QStringLiteral("--refine"),
                                 QStringLiteral("--cleanup"), QStringLiteral("--profile"),
                                 QStringLiteral("--tone"),    QStringLiteral("--language"),
                                 QStringLiteral("--raw"),     QStringLiteral("--json")};
        // Read off --help by hand.
        const QMap<QString, QStringList> documented{
            {QString(), {QStringLiteral("--daemon"), QStringLiteral("--version"), QStringLiteral("--help")}},
            {QStringLiteral("toggle"), dictation},
            {QStringLiteral("start"), dictation},
            {QStringLiteral("stop"), {}},
            {QStringLiteral("cancel"), {}},
            {QStringLiteral("status"), {QStringLiteral("--json"), QStringLiteral("--watch")}},
            {QStringLiteral("last"), {}},
            {QStringLiteral("vocabulary add"), {}},
            {QStringLiteral("settings"), {}},
            {QStringLiteral("setup"), {}},
            {QStringLiteral("quit"), {}},
            {QStringLiteral("transcribe"),
             shared
                 + QStringList{QStringLiteral("--headless"), QStringLiteral("--output"), QStringLiteral("--stdout"),
                               QStringLiteral("--srt"), QStringLiteral("--vtt")}},
            {QStringLiteral("listen"), shared + QStringList{QStringLiteral("--until-silence")}},
            {QStringLiteral("providers"), {QStringLiteral("--json")}},
            {QStringLiteral("record start"),
             {QStringLiteral("--to"), QStringLiteral("--mic-only"), QStringLiteral("--vocab-file")}},
            {QStringLiteral("record status"), {QStringLiteral("--json")}},
            {QStringLiteral("record stop"), {}},
            {QStringLiteral("completions bash"), {}},
            {QStringLiteral("completions zsh"), {}},
            {QStringLiteral("completions fish"), {}},
        };

        // The table above has every command and option --help has: a command
        // starts a line, after two spaces and maybe "speecher", and ends it or
        // comes before its arguments or description, unlike a line of prose;
        // words between | are alternatives. The options in brackets after a
        // command are all it takes.
        int exitCode = -1;
        const QString help = printedBy({QStringLiteral("--help")}, &exitCode);
        QStringList helpCommands;
        const QRegularExpression commandLine(
            QStringLiteral("^  (?:speecher )?([a-z][a-z|]*(?: [a-z|]+)*)(?=  | [\\[<]|$)(.*)$"),
            QRegularExpression::MultilineOption);
        const QRegularExpression bracketedOption(QStringLiteral("\\[(--[a-z][a-z-]*)"));
        for (const QRegularExpressionMatch &match : commandLine.globalMatch(help)) {
            QStringList commands{QString()};
            for (const QString &word :
                 match.captured(1).replace(QStringLiteral(" | "), QStringLiteral("|")).split(QLatin1Char(' '))) {
                QStringList longer;
                for (const QString &command : std::as_const(commands)) {
                    for (const QString &alternative : word.split(QLatin1Char('|'))) {
                        longer << (command + QLatin1Char(' ') + alternative).trimmed();
                    }
                }
                commands = longer;
            }
            QStringList bracketed;
            for (const QRegularExpressionMatch &option : bracketedOption.globalMatch(match.captured(2))) {
                bracketed << option.captured(1);
            }
            if (!bracketed.isEmpty()) {
                for (const QString &command : std::as_const(commands)) {
                    QCOMPARE(documented.value(command), bracketed);
                }
            }
            helpCommands << commands;
        }
        helpCommands.removeDuplicates();
        helpCommands.prepend(QString());
        helpCommands.sort();
        QCOMPARE(helpCommands, documented.keys());
        QStringList documentedOptions;
        for (const QStringList &options : documented) {
            documentedOptions << options;
        }
        for (const QRegularExpressionMatch &match :
             QRegularExpression(QStringLiteral("--[a-z][a-z-]*")).globalMatch(help)) {
            QVERIFY2(documentedOptions.contains(match.captured()), qPrintable(match.captured()));
        }

        // What each script offers after a command: its options, when the word
        // typed starts with -, or else its arguments. bash's is run; zsh's and
        // fish's are read off the script.
#ifdef Q_OS_WIN
        // The bash on a Windows PATH may be WSL's launcher, with no Linux to
        // run it in.
        const bool haveBash = false;
#else
        const bool haveBash = !QStandardPaths::findExecutable(QStringLiteral("bash")).isEmpty();
#endif
        const auto offered = [](const QString &shell, const QString &script, const QString &command, bool options) {
            if (shell == QStringLiteral("bash")) {
                return bashCompletions(script, QStringList{QStringLiteral("speecher")}
                                                   + command.split(QLatin1Char(' '), Qt::SkipEmptyParts)
                                                   + QStringList{options ? QStringLiteral("-") : QString()});
            }
            const QString escaped = QRegularExpression::escape(command);
            const QString pattern = shell == QStringLiteral("zsh")
                ? (options ? QStringLiteral("^    \\('%1'\\) options=\\(([^)]*)\\) ;;$")
                           : QStringLiteral("^    \\('%1'\\) compadd -- ([^;]*)"))
                : (options ? QStringLiteral("^complete -c speecher -n '__speecher_is%1' -l (\\S+)")
                           : QStringLiteral("^complete -c speecher -n '__speecher_is%1' -a '([^']*)'"));
            QStringList words;
            const QRegularExpression found(
                pattern.arg(shell == QStringLiteral("fish") && !command.isEmpty() ? QLatin1Char(' ') + escaped
                                                                                   : escaped),
                QRegularExpression::MultilineOption);
            for (const QRegularExpressionMatch &match : found.globalMatch(script)) {
                for (const QString &word : match.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                    words << (shell == QStringLiteral("fish") && options ? QStringLiteral("--") + word : word);
                }
            }
            return words;
        };
        for (const QString &shell : {QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish")}) {
            if (shell == QStringLiteral("bash") && !haveBash) {
                continue;
            }
            const QString script = printedBy({QStringLiteral("completions"), shell}, &exitCode);
            QCOMPARE(exitCode, 0);
            for (auto command = documented.cbegin(); command != documented.cend(); ++command) {
                const QStringList given = offered(shell, script, command.key(), true);
                for (const QString &option : command.value()) {
                    QVERIFY2(given.contains(option),
                             qPrintable(QStringLiteral("%1: speecher %2 %3").arg(shell, command.key(), option)));
                }
                if (command.key().isEmpty()) {
                    continue;
                }
                const QString parent = command.key().section(QLatin1Char(' '), 0, -2);
                QVERIFY2(offered(shell, script, parent, false).contains(command.key().section(QLatin1Char(' '), -1)),
                         qPrintable(QStringLiteral("%1: speecher %2").arg(shell, command.key())));
            }
        }

        std::ostringstream usage;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        for (const QStringList &mistake : {QStringList{}, QStringList{QStringLiteral("tcsh")},
                                           QStringList{QStringLiteral("--list"), QStringLiteral("files")}}) {
            printedBy(QStringList{QStringLiteral("completions")} + mistake, &exitCode);
            QCOMPARE(exitCode, 2);
        }
    }

    // The scripts read these while completing, so a profile, tone or level
    // added later is offered too, by the name the options take.
    void completionsListTheSettingsNames()
    {
        SettingsStore settings;
        settings.raw().clear();
        AppSettings draft = settings.snapshot();
        draft.refinement.writingProfiles.append({QStringLiteral("custom_stand_up"), QStringLiteral("balanced"),
                                                 QStringLiteral("none"), QString(), QStringLiteral("Stand up")});
        draft.refinement.customTones = {
            {QStringLiteral("custom_very_terse"), QStringLiteral("Very terse"), QStringLiteral("Short.")}};
        draft.refinement.customCleanupLevels = {{QStringLiteral("custom_notes"), QStringLiteral("Notes"),
                                                 QStringLiteral("balanced"), QString()}};
        settings.applySnapshot(draft);
        const auto listed = [](const QString &list) {
            return printedBy({QStringLiteral("completions"), QStringLiteral("--list"), list})
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        };
        QCOMPARE(listed(QStringLiteral("profiles")),
                 QStringList({QStringLiteral("Work"), QStringLiteral("Email"), QStringLiteral("Personal"),
                              QStringLiteral("AI-coding"), QStringLiteral("Other"), QStringLiteral("Stand-up")}));
        QCOMPARE(listed(QStringLiteral("tones")).last(), QStringLiteral("very-terse"));
        QCOMPARE(listed(QStringLiteral("cleanup")),
                 QStringList({QStringLiteral("none"), QStringLiteral("light"), QStringLiteral("medium"),
                              QStringLiteral("high"), QStringLiteral("notes")}));
        settings.raw().clear();
    }

    // Where two profiles share a command-line name, --profile takes it for
    // neither, so each is offered by its id; a built-in, whose id is its
    // name, is not offered at all. So is a name the command line would read
    // as another option. Every name offered picks its profile.
    void completedProfileNamesPickTheirProfile()
    {
        SettingsStore settings;
        settings.raw().clear();
        const auto restoreSettings = qScopeGuard([&settings] { settings.raw().clear(); });
        AppSettings draft = settings.snapshot();
        for (const auto &[id, name] : {std::pair{QStringLiteral("custom_stand_up"), QStringLiteral("Stand up")},
                                       std::pair{QStringLiteral("custom_standup"), QStringLiteral("Stand-up")},
                                       std::pair{QStringLiteral("custom_work"), QStringLiteral("work")},
                                       std::pair{QStringLiteral("custom_toms"), QStringLiteral("Tom's  notes")},
                                       std::pair{QStringLiteral("custom_help"), QStringLiteral("--help")},
                                       std::pair{QStringLiteral("custom_version"), QStringLiteral("--version")},
                                       std::pair{QStringLiteral("custom_daemon"), QStringLiteral("--daemon")}}) {
            draft.refinement.writingProfiles.append(
                {id, QStringLiteral("balanced"), QStringLiteral("none"), QString(), name});
        }
        settings.applySnapshot(draft);

        const QStringList names =
            printedBy({QStringLiteral("completions"), QStringLiteral("--list"), QStringLiteral("profiles")})
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(names, QStringList({QStringLiteral("Email"), QStringLiteral("Personal"), QStringLiteral("AI-coding"),
                                     QStringLiteral("Other"), QStringLiteral("custom_stand_up"),
                                     QStringLiteral("custom_standup"), QStringLiteral("custom_work"),
                                     QStringLiteral("Tom's-notes"), QStringLiteral("custom_help"),
                                     QStringLiteral("custom_version"), QStringLiteral("custom_daemon")}));
        const QStringList ids{QStringLiteral("email"),          QStringLiteral("personal"),
                              QStringLiteral("ai_coding"),      QStringLiteral("other"),
                              QStringLiteral("custom_stand_up"), QStringLiteral("custom_standup"),
                              QStringLiteral("custom_work"),    QStringLiteral("custom_toms"),
                              QStringLiteral("custom_help"),    QStringLiteral("custom_version"),
                              QStringLiteral("custom_daemon")};
        for (qsizetype index = 0; index < names.size(); ++index) {
            const CommandLineDecision decision = parseCommandLine(
                {QStringLiteral("speecher"), QStringLiteral("toggle"), QStringLiteral("--profile"), names.at(index)},
                {});
            QCOMPARE(decision.sessionOverrides.writingProfile, std::optional(ids.at(index)));
        }
    }

    // bash offers a listed name as it is, escaped for the command line or
    // for the quote the word typed opens, never running or expanding what is
    // in it, with bash-completion loaded or not.
    void bashCompletesListedNamesLiterally()
    {
#ifdef Q_OS_WIN
        QSKIP("The fake speecher is a shell script");
#endif
        if (QStandardPaths::findExecutable(QStringLiteral("bash")).isEmpty()) {
            QSKIP("bash is not installed");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A glob would match these.
        for (const QString &name : {QStringLiteral("globa"), QStringLiteral("x")}) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(!fakeSpeecher(dir.path(), QStringLiteral("Demo$(id>ran)\nDemo`id>ran2`\nTom's-notes\n$HOME\n~\n"
                                                         "{a,b}\n*\nglob[ab]\nAI:-notes\n"))
                     .isEmpty());

        const QString script = printedBy({QStringLiteral("completions"), QStringLiteral("bash")});
        const QString bashCompletion = QStringLiteral("/usr/share/bash-completion/bash_completion");
        QStringList preludes{QString()};
        if (QFile::exists(bashCompletion)) {
            preludes << QStringLiteral("source ") + bashCompletion;
        }
        for (const QString &prelude : std::as_const(preludes)) {
            // speecher is run from HOME, as ~/speecher.
            const auto offered = [&](const QStringList &typed, const QString &line = {}) {
                return bashCompletions(script,
                                       QStringList{QStringLiteral("~/speecher"), QStringLiteral("transcribe"),
                                                   QStringLiteral("--profile")}
                                           + typed,
                                       dir.path(), line, prelude);
            };
            QCOMPARE(offered({QString()}),
                     QStringList({QStringLiteral("Demo\\$\\(id\\>ran\\)"), QStringLiteral("Demo\\`id\\>ran2\\`"),
                                  QStringLiteral("Tom\\'s-notes"), QStringLiteral("\\$HOME"), QStringLiteral("\\~"),
                                  QStringLiteral("\\{a\\,b\\}"), QStringLiteral("\\*"),
                                  QStringLiteral("glob\\[ab\\]"), QStringLiteral("AI:-notes")}));
            QCOMPARE(offered({QStringLiteral("Tom")}), QStringList{QStringLiteral("Tom\\'s-notes")});
            QCOMPARE(offered({QStringLiteral("$")}), QStringList{QStringLiteral("\\$HOME")});
            QCOMPARE(offered({QStringLiteral("Demo\\")}),
                     QStringList({QStringLiteral("Demo\\$\\(id\\>ran\\)"), QStringLiteral("Demo\\`id\\>ran2\\`")}));
            // bash puts back the quote the word opens.
            QCOMPARE(offered({QStringLiteral("\"Tom")}), QStringList{QStringLiteral("Tom's-notes")});
            QCOMPARE(offered({QStringLiteral("'Tom")}), QStringList{QStringLiteral("Tom'\\''s-notes")});
            QCOMPARE(offered({QStringLiteral("\"Demo")}),
                     QStringList({QStringLiteral("Demo\\$(id>ran)"), QStringLiteral("Demo\\`id>ran2\\`")}));
            // bash splits AI: in two and replaces what follows the :.
            QCOMPARE(offered({QStringLiteral("AI"), QStringLiteral(":")},
                             QStringLiteral("~/speecher transcribe --profile AI:")),
                     QStringList{QStringLiteral("-notes")});
        }
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("ran"))));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("ran2"))));
    }

    // zsh asks speecher for the profiles each time it completes them, so it
    // offers one added after its script was generated.
    void zshCompletesProfilesAddedLater()
    {
#ifdef Q_OS_WIN
        QSKIP("The fake speecher is a shell script");
#endif
        if (QStandardPaths::findExecutable(QStringLiteral("zsh")).isEmpty()) {
            QSKIP("zsh is not installed");
        }
        const QString script = printedBy({QStringLiteral("completions"), QStringLiteral("zsh")});
        SettingsStore settings;
        settings.raw().clear();
        const auto restoreSettings = qScopeGuard([&settings] { settings.raw().clear(); });
        AppSettings draft = settings.snapshot();
        draft.refinement.writingProfiles.append({QStringLiteral("custom_stand_up"), QStringLiteral("balanced"),
                                                 QStringLiteral("none"), QString(), QStringLiteral("Stand up")});
        settings.applySnapshot(draft);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString speecher = fakeSpeecher(
            dir.path(),
            printedBy({QStringLiteral("completions"), QStringLiteral("--list"), QStringLiteral("profiles")}));
        QVERIFY(!speecher.isEmpty());

        const QStringList offered =
            zshCompletions(script, {speecher, QStringLiteral("transcribe"), QStringLiteral("--profile"), QString()});
        QVERIFY2(offered.contains(QStringLiteral("Stand-up")), qPrintable(offered.join(QLatin1Char(' '))));
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

    // Every argument after `vocabulary add` is a term; none is a dictation option.
    void vocabularyAddIsAClientCommand()
    {
        const auto parse = [](QStringList arguments) {
            return parseCommandLine(QStringList{QStringLiteral("speecher"), QStringLiteral("vocabulary")} + arguments,
                                    {});
        };
        const CommandLineDecision decision =
            parse({QStringLiteral("add"), QStringLiteral("FileTranscriptionSession"), QStringLiteral("Speecher CLI")});
        QCOMPARE(decision.mode, LaunchMode::RunCli);
        QCOMPARE(decision.ipcCommand, IpcCommand::AddVocabulary);
        QCOMPARE(decision.vocabularyTerms,
                 QStringList({QStringLiteral("FileTranscriptionSession"), QStringLiteral("Speecher CLI")}));
        QCOMPARE(parse({QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("-fsanitize")}).vocabularyTerms,
                 QStringList({QStringLiteral("-fsanitize")}));
        // Not even the options every command takes.
        for (const QString &term : {QStringLiteral("--version"), QStringLiteral("--help"), QStringLiteral("-h"),
                                    QStringLiteral("--daemon")}) {
            const CommandLineDecision escaped = parse({QStringLiteral("add"), QStringLiteral("--"), term});
            QCOMPARE(escaped.mode, LaunchMode::RunCli);
            QCOMPARE(escaped.vocabularyTerms, QStringList({term}));
        }

        std::ostringstream usage;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(usage.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        for (const QStringList &mistake : {QStringList{},
                                           QStringList{QStringLiteral("list")},
                                           QStringList{QStringLiteral("add")},
                                           QStringList{QStringLiteral("add"), QStringLiteral(" ")},
                                           QStringList{QStringLiteral("add"), QStringLiteral("--")},
                                           QStringList{QStringLiteral("add"), QStringLiteral("-fsanitize")},
                                           QStringList{QStringLiteral("add"), QStringLiteral("KWin"),
                                                       QStringLiteral("--profile"), QStringLiteral("ai-coding")}}) {
            const CommandLineDecision refused = parse(mistake);
            QCOMPARE(refused.mode, LaunchMode::Exit);
            QCOMPARE(refused.exitCode, 2);
        }
    }

    void quitIsAClientCommand()
    {
        const CommandLineDecision decision = parseCommandLine(
            {QStringLiteral("speecher"), QStringLiteral("quit")}, {});
        QCOMPARE(decision.mode, LaunchMode::RunCli);
        QCOMPARE(decision.ipcCommand, IpcCommand::Quit);

        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QSignalSpy requested(&controller, &ApplicationController::quitRequested);
        controller.ipcCommands()->handle({IpcCommand::Quit}, nullptr);
        QCOMPARE(requested.count(), 1);
    }

#endif

#ifdef Q_OS_LINUX

    void appImageIntegrationRemovalDeletesWhatAnInstallLeftAndReportsIt()
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
        const QString appImage = root.filePath(QStringLiteral("Speecher.AppImage"));
        QVERIFY(writeFixtureFile(appImage, "image"));
        // What an earlier release's install left in the home folder.
        const QString desktopFile = home.filePath(
            QStringLiteral(".local/share/applications/io.github.firemonster612.speecher.desktop"));
        const QString icon = home.filePath(
            QStringLiteral(".local/share/icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"));
        const QString link = home.filePath(QStringLiteral(".local/bin/speecher"));
        const QString helper = home.filePath(
            QStringLiteral(".local/share/speecher/libexec/speecher-ydotool-setup"));
        QVERIFY(writeFixtureFile(desktopFile, "[Desktop Entry]\nExec=speecher\n"));
        QVERIFY(writeFixtureFile(icon, "<svg/>"));
        QVERIFY(writeFixtureFile(helper, "helper"));
        QVERIFY(QDir().mkpath(QFileInfo(link).path()));
        QVERIFY(QFile::link(appImage, link));

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
        QVERIFY(writeFixtureFile(link, "#!/bin/sh\n"));
        removal = removeAppImageIntegration(home.path());
        QCOMPARE(removal.failed.size(), 1);
        QVERIFY(removal.failed.first().startsWith(QStringLiteral("speecher command")));
        QVERIFY(QFile::exists(link));

        QVERIFY(QFile::remove(link));
        const QString renamed = root.filePath(QStringLiteral("dictation"));
        QVERIFY(writeFixtureFile(renamed, "image"));
        QVERIFY(QFile::link(renamed, link));
        qputenv("APPIMAGE", QFile::encodeName(renamed));
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.removed.contains(QStringLiteral("speecher command")));
        QVERIFY(!QFileInfo(link).isSymLink());

        qunsetenv("APPIMAGE");
        const QString unrelated = home.filePath(QStringLiteral("SomeoneElse.AppImage"));
        QVERIFY(writeFixtureFile(unrelated, "image"));
        QVERIFY(QFile::link(unrelated, link));
        removal = removeAppImageIntegration(home.path());
        QVERIFY(removal.failed.contains(QStringLiteral(
            "speecher command: the link does not point to a Speecher AppImage")));
        QVERIFY(QFileInfo(link).isSymLink());
    }

    // A custom XDG_DATA_HOME is where this desktop looked for user-installed
    // launchers, so the install put the menu entry and icon there.
    void appImageIntegrationRemovalFollowsXdgDataHome()
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

        const QString desktopFile = QDir(dataHome).filePath(
            QStringLiteral("applications/io.github.firemonster612.speecher.desktop"));
        const QString icon = QDir(dataHome).filePath(
            QStringLiteral("icons/hicolor/scalable/apps/io.github.firemonster612.speecher.svg"));
        QVERIFY(writeFixtureFile(desktopFile, "[Desktop Entry]\nExec=speecher\n"));
        QVERIFY(writeFixtureFile(icon, "<svg/>"));

        const DesktopIntegrationRemoval removal = removeAppImageIntegration(home);
        QVERIFY(removal.removed.contains(QStringLiteral("app menu entry")));
        QVERIFY(removal.removed.contains(QStringLiteral("app icon")));
        QVERIFY(removal.failed.isEmpty());
        QVERIFY(!QFile::exists(desktopFile));
        QVERIFY(!QFile::exists(icon));
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
        SettingsStore settings;
        QList<GlobalShortcutBinder *> binders{new KGlobalAccelShortcutBinder(),
                                              new PortalGlobalShortcutBinder(&settings)};
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
        QSignalSpy accessibilityChanged(controller.accessibility(), &AccessibilityMonitor::stateChanged);
        QVERIFY(platform->binder);
        QCOMPARE(platform->binder->bindCount, 0);

        controller.frontEndReady();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);

        QCOMPARE(platform->binder->bindCount, 1);
        QVERIFY(controller.accessibility()->supported());
        QVERIFY(controller.accessibility()->enabled());
        QVERIFY(!controller.accessibility()->persistent());

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
        QSignalSpy accessibilityChanged(controller.accessibility(), &AccessibilityMonitor::stateChanged);

        controller.frontEndReady();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);
        QVERIFY(!controller.accessibility()->enabled());
        QVERIFY(platform->accessibilityRefresh);

        platform->accessibility = {true, true, true};
        platform->accessibilityRefresh();

        QVERIFY(controller.accessibility()->enabled());
        QVERIFY(controller.accessibility()->persistent());
        QCOMPARE(accessibilityChanged.count(), 2);
        QCOMPARE(accessibilityChanged.last().at(1).toBool(), true);
    }

    void windowRequestsGoToTheFrontEnd()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });

        controller.showMain();
        controller.showSettings();
        controller.showSetup();

        QCOMPARE(frontEnd.calls,
                 QStringList({QStringLiteral("showMainWindow"),
                              QStringLiteral("showSettingsWindow"),
                              QStringLiteral("showSetupAssistant")}));
    }

    void aFinishedModelDownloadIsAnnouncedThroughTheFrontEnd()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        LocalSpeechSettings local = controller.settings()->dictationSnapshot().speech.local;
        local.modelId = QStringLiteral("moonshine-small");
        controller.settings()->edit([&](AppSettings &draft) {
            draft.speech.local = local;
            draft.speech.providerId = QStringLiteral("local");
            return true;
        });

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
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = false; return true; });

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
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });

        controller.ipcCommands()->handle(
            {IpcCommand::Transcribe, {}, {QStringLiteral("/a.wav"), QStringLiteral("/b.mp3")}}, nullptr);

        QCOMPARE(frontEnd.calls, QStringList({QStringLiteral("showTranscribeFiles /a.wav /b.mp3")}));
    }

    // The running app answers providers with the sign-ins it has seen.
    void providersCommandAnswersWithTheSignInsTheAppHasSeen()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        controller.providerAvailability()->noteSignIn(QStringLiteral("codex"), false);

        QLocalServer server;
        QVERIFY(server.listen(QStringLiteral("spchr-p-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(12))));
        QLocalSocket client;
        client.connectToServer(server.fullServerName());
        QVERIFY(server.waitForNewConnection(2000));
        controller.ipcCommands()->handle({IpcCommand::Providers}, server.nextPendingConnection());
        QVERIFY(client.waitForReadyRead(2000));

        const QJsonObject answer = QJsonDocument::fromJson(client.readLine()).object();
        QVERIFY(answer.value(QStringLiteral("ok")).toBool());
        const QJsonArray reports =
            QJsonDocument::fromJson(answer.value(QStringLiteral("text")).toString().toUtf8()).array();
        const auto codex = std::find_if(reports.cbegin(), reports.cend(), [](const QJsonValue &report) {
            return report[QStringLiteral("id")] == QStringLiteral("codex")
                && report[QStringLiteral("role")] == QStringLiteral("speech");
        });
        QVERIFY(codex != reports.cend());
        QCOMPARE((*codex)[QStringLiteral("signedIn")], QJsonValue(false));
        QCOMPARE((*codex)[QStringLiteral("problem")], QJsonValue(QStringLiteral("Not signed in to ChatGPT.")));
    }

    void theControllerRefusesARequestItCannotServe_data()
    {
        QTest::addColumn<QByteArray>("requestBytes");
        QTest::addColumn<QByteArray>("responseBytes");
        // Byte for byte what the previous release answered: its command line
        // reads "Unknown command" as an instance older than the command.
        QTest::newRow("unknown command")
            << QByteArrayLiteral(R"({"command":"rewind"})" "\n")
            << QByteArrayLiteral(R"({"message":"Unknown command","ok":false,"state":"idle"})" "\n");
        QTest::newRow("unknown command with a field of another shape")
            << QByteArrayLiteral(R"({"command":"rewind","files":"x","spokenLanguage":"xx"})" "\n")
            << QByteArrayLiteral(R"({"message":"Unknown command","ok":false,"state":"idle"})" "\n");
        QTest::newRow("unknown output format")
            << QByteArrayLiteral(R"({"command":"toggle","outputFormat":"rtf"})" "\n")
            << QByteArrayLiteral(R"({"message":"Unknown output format","ok":false,"state":"idle"})" "\n");
        // The previous release left this unanswered until the client gave up.
        QTest::newRow("not JSON")
            << QByteArrayLiteral("toggle\n")
            << QByteArrayLiteral(R"({"message":"Invalid request","ok":false,"state":"idle"})" "\n");
    }

    void theControllerRefusesARequestItCannotServe()
    {
        QFETCH(QByteArray, requestBytes);
        QFETCH(QByteArray, responseBytes);
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->ipcName = uniqueControllerIpcName();
        ApplicationController controller(true, platform);
        QCOMPARE(controller.ipcCommands()->listen(), IpcListenResult::Listening);

        QLocalSocket client;
        connectToController(client, platform->ipcName);
        client.write(requestBytes);
        QTRY_VERIFY(client.canReadLine());
        QCOMPARE(client.readLine(), responseBytes);
    }

    // The controller answers status on its own IPC with the recording, and
    // writes a watcher a line on each change of the dictation state. A
    // watcher's further commands are ignored: none doubles its lines or ends
    // its watch.
    void theControllerWritesItsStatusToWatchers()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->ipcName = uniqueControllerIpcName();
        ApplicationController controller(true, platform);
        QCOMPARE(controller.ipcCommands()->listen(), IpcListenResult::Listening);

        QLocalSocket status;
        connectToController(status, platform->ipcName);
        status.write(QByteArrayLiteral("{\"command\":\"status\"}\n"));
        QTRY_VERIFY(status.canReadLine());
        const QJsonObject answer = QJsonDocument::fromJson(status.readLine()).object();
        QCOMPARE(answer.value(QStringLiteral("state")).toString(), QStringLiteral("idle"));
        QCOMPARE(answer.value(QStringLiteral("recording"))[QStringLiteral("recording")], QJsonValue(false));

        QLocalSocket watcher;
        connectToController(watcher, platform->ipcName);
        watcher.write(QByteArrayLiteral("{\"command\":\"watchStatus\"}\n{\"command\":\"watchStatus\"}\n"));
        QTRY_VERIFY(watcher.canReadLine());
        watcher.write(QByteArrayLiteral("{\"command\":\"status\"}\n"));
        // An unknown speech provider fails the session as it starts.
        controller.settings()->edit([](AppSettings &draft) {
            draft.speech.providerId = QStringLiteral("missing");
            return true;
        });
        controller.session()->startListening();
        QTRY_COMPARE(controller.session()->stateName(), QStringLiteral("error"));

        QStringList states;
        for (const QJsonObject &line : linesWritten(watcher)) {
            states << line.value(QStringLiteral("state")).toString();
        }
        QCOMPARE(states, QStringList({QStringLiteral("idle"), QStringLiteral("starting"), QStringLiteral("error")}));
        QCOMPARE(watcher.state(), QLocalSocket::ConnectedState);
    }

    // A recording's stream changing writes a watcher one line, and audio
    // lost past the buffer one more, however many chunks it goes on losing.
    void aRecordingWritesWatchersALinePerStreamChange()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->ipcName = uniqueControllerIpcName();
        QPointer<test::FakeAudioInput> microphone;
        platform->audioInputs = [&microphone](QObject *parent) {
            microphone = new test::FakeAudioInput(parent);
            return microphone.data();
        };
        ApplicationController controller(true, platform);
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.speech.providerId = QStringLiteral("codex");
            return true;
        });
        QPointer<test::FakeSpeechTranscriber> codex;
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")}, [&codex](QObject *parent) {
                codex = new test::FakeSpeechTranscriber(parent);
                codex->providerId = QStringLiteral("codex");
                codex->streamsFinals = true;
                codex->onStartAttempt = [codex = codex.data()] { codex->emitConnected(); };
                return codex.data();
            });
        QCOMPARE(controller.ipcCommands()->listen(), IpcListenResult::Listening);
        QTemporaryDir dir;

        QLocalSocket start;
        connectToController(start, platform->ipcName);
        start.write(QJsonDocument(QJsonObject{{QStringLiteral("command"), QStringLiteral("recordStart")},
                                              {QStringLiteral("files"),
                                               QJsonArray{dir.filePath(QStringLiteral("call.md"))}}})
                        .toJson(QJsonDocument::Compact)
                    + '\n');
        QTRY_VERIFY(platform->microphoneAnswer);
        platform->microphoneAnswer(true);
        QTRY_VERIFY(start.canReadLine());
        QVERIFY(controller.isRecording());

        QLocalSocket watcher;
        connectToController(watcher, platform->ipcName);
        watcher.write(QByteArrayLiteral("{\"command\":\"watchStatus\"}\n"));
        QCOMPARE(linesWritten(watcher).size(), 1);
        const auto stream = [](const QJsonObject &line) {
            return line.value(QStringLiteral("recording"))[QStringLiteral("streams")][0];
        };

        // The stream drops and does not come back.
        codex->onStartAttempt = nullptr;
        codex->emitFailure(QStringLiteral("Connection reset"), true, SpeechPhase::Streaming,
                           ProviderFailureKind::Network);
        const QList<QJsonObject> dropped = linesWritten(watcher);
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(stream(dropped.first())[QStringLiteral("state")], QJsonValue(QStringLiteral("reconnecting")));

        // Ten minutes wait for it, in ten-second chunks; the four past them
        // each lose audio.
        const QByteArray tenSeconds(10 * 16000 * 2, '\0');
        for (int chunk = 0; chunk < 64; ++chunk) {
            microphone->pushAudio(tenSeconds);
        }
        const QList<QJsonObject> losing = linesWritten(watcher);
        QCOMPARE(losing.size(), 1);
        QVERIFY(stream(losing.first())[QStringLiteral("lostAudioMs")].toInteger() > 0);
    }

    // A record start whose command line stopped waiting, and so reported a
    // failure, leaves nothing recording and no file once the stream connects.
    void aRecordStartNoOneWaitsForIsDiscarded()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->audioInputs = [](QObject *parent) { return new test::FakeAudioInput(parent); };
        ApplicationController controller(true, platform);
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.speech.providerId = QStringLiteral("codex");
            return true;
        });
        bool connected = false;
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")}, [&connected](QObject *parent) {
                auto *codex = new test::FakeSpeechTranscriber(parent);
                codex->providerId = QStringLiteral("codex");
                codex->streamsFinals = true;
                codex->onStartAttempt = [codex, &connected] {
                    codex->emitConnected();
                    connected = true;
                };
                return codex;
            });
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("call.md"));

        controller.ipcCommands()->handle({IpcCommand::RecordStart, {}, {path}}, nullptr);
        platform->microphoneAnswer(true);
        QTRY_VERIFY(connected);
        QVERIFY(!controller.isRecording());
        QVERIFY(!QFileInfo::exists(path));
    }

    // Outside an AppImage, the AppImage updater's restart opens the release
    // page instead, which a test can count. The other updaters quit or start
    // an installer.
    static ManifestUpdater *restartWatchableUpdater(ApplicationController &controller)
    {
        return controller.updates()->inherits("speecher::AppImageUpdater")
            ? static_cast<ManifestUpdater *>(controller.updates())
            : nullptr;
    }

    // An update restart asked for mid-recording waits for the recording, and
    // the stop's reply still reaches its caller before the app goes away.
    void anUpdateRestartWaitsForARecordingToStop()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        platform->audioInputs = [](QObject *parent) { return new test::FakeAudioInput(parent); };
        ApplicationController controller(true, platform);
        auto *updater = restartWatchableUpdater(controller);
        if (!updater) {
            QSKIP("Only the AppImage updater's restart can be watched without quitting");
        }
        controller.settings()->edit([](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.speech.providerId = QStringLiteral("codex");
            return true;
        });
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT Codex")}, [](QObject *parent) {
                auto *codex = new test::FakeSpeechTranscriber(parent);
                codex->providerId = QStringLiteral("codex");
                codex->streamsFinals = true;
                codex->onStartAttempt = [codex] { codex->emitConnected(); };
                return codex;
            });
        QLocalServer server;
        QVERIFY(server.listen(QStringLiteral("spchr-u-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(12))));
        QTemporaryDir dir;

        ManifestUpdaterTestAccess::setState(*updater, UpdateController::State::ReadyToRestart);
        QLocalSocket startClient;
        startClient.connectToServer(server.fullServerName());
        QVERIFY(server.waitForNewConnection(2000));
        controller.ipcCommands()->handle({IpcCommand::RecordStart, {}, {dir.filePath(QStringLiteral("call.md"))}},
                                         server.nextPendingConnection());
        platform->microphoneAnswer(true);
        QTRY_VERIFY(controller.isRecording());
        QCOMPARE(updater->restartBlocker(), UpdateController::RestartBlocker::Recording);

        QLocalSocket *stopReply = nullptr;
        bool stopRepliedFirst = false;
        connect(updater, &UpdateController::openReleasePageRequested, this, [&] {
            stopRepliedFirst = stopReply && stopReply->state() != QLocalSocket::ConnectedState;
        });
        QSignalSpy restartAttempts(updater, &UpdateController::openReleasePageRequested);
        updater->installAndRestart();
        QCOMPARE(updater->state(), UpdateController::State::RestartPending);
        QVERIFY(restartAttempts.isEmpty());

        QLocalSocket stopClient;
        stopClient.connectToServer(server.fullServerName());
        QVERIFY(server.waitForNewConnection(2000));
        stopReply = server.nextPendingConnection();
        controller.ipcCommands()->handle({IpcCommand::RecordStop}, stopReply);
        QTRY_COMPARE(restartAttempts.count(), 1);
        QVERIFY(stopRepliedFirst);
        QTRY_VERIFY(stopClient.canReadLine());
        QVERIFY(QJsonDocument::fromJson(stopClient.readLine()).object().value(QStringLiteral("ok")).toBool());
    }

    // An update restart asked for mid-dictation waits for it, and a dictation
    // that ends in an error has nothing left to cut short.
    void anUpdateRestartWaitsForADictationButNotItsError()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        auto *updater = restartWatchableUpdater(controller);
        if (!updater) {
            QSKIP("Only the AppImage updater's restart can be watched without quitting");
        }
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        emit platform->binder->activated();
        platform->microphoneAnswer(true);
        QCOMPARE(controller.session()->state(), DictationState::Starting);

        ManifestUpdaterTestAccess::setState(*updater, UpdateController::State::ReadyToRestart);
        QSignalSpy restartAttempts(updater, &UpdateController::openReleasePageRequested);
        updater->installAndRestart();
        QCOMPARE(updater->state(), UpdateController::State::RestartPending);

        auto *audio = controller.findChild<AudioInput *>();
        QVERIFY(audio);
        emit audio->failed(QStringLiteral("Test microphone disconnected"));
        QCOMPARE(controller.session()->state(), DictationState::Error);
        QCOMPARE(restartAttempts.count(), 1);
    }

    // An update restart asked for while files are transcribed waits for the
    // batch to finish.
    void anUpdateRestartWaitsForAFileTranscription()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        auto *updater = restartWatchableUpdater(controller);
        if (!updater) {
            QSKIP("Only the AppImage updater's restart can be watched without quitting");
        }
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("silence.wav"));
        QVERIFY(writeSilentWav(audio));
        TranscribeOptions options;
        options.speechProviderId = QStringLiteral("claude");
        QVERIFY(controller.fileTranscription()->start({audio}, options));
        QCOMPARE(updater->restartBlocker(), UpdateController::RestartBlocker::FileTranscription);

        ManifestUpdaterTestAccess::setState(*updater, UpdateController::State::ReadyToRestart);
        QSignalSpy restartAttempts(updater, &UpdateController::openReleasePageRequested);
        updater->installAndRestart();
        QCOMPARE(updater->state(), UpdateController::State::RestartPending);
        QVERIFY(restartAttempts.isEmpty());

        controller.fileTranscription()->cancel();
        QCOMPARE(restartAttempts.count(), 1);
    }

#ifdef Q_OS_LINUX
    // A restart that waited for a batch keeps waiting while a transcript the
    // batch made is only on screen, and goes once the Transcribe page exports
    // it or goes back to setup.
    void anUpdateRestartWaitsForAnUnsavedTranscript_data()
    {
        QTest::addColumn<QString>("release");
        QTest::newRow("export-one") << QStringLiteral("export-one");
        QTest::newRow("export-all") << QStringLiteral("export-all");
        QTest::newRow("back-to-setup") << QStringLiteral("back-to-setup");
    }

    void anUpdateRestartWaitsForAnUnsavedTranscript()
    {
        QFETCH(QString, release);
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        auto *updater = restartWatchableUpdater(controller);
        if (!updater) {
            QSKIP("Only the AppImage updater's restart can be watched without quitting");
        }
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("claude"), QStringLiteral("Claude")}, [](QObject *parent) {
                auto *claude = new test::FakeSpeechTranscriber(parent);
                claude->onStartAttempt = [claude] { claude->emitFinalText(QStringLiteral("hello")); };
                return claude;
            });
        QTemporaryDir dir;
        const QString audio = dir.filePath(QStringLiteral("silence.wav"));
        QVERIFY(writeSilentWav(audio));
        TranscribeQueue &queue = *controller.transcribeQueue();
        queue.addFiles({audio});
        TranscribeOptions options = queue.options();
        options.speechProviderId = QStringLiteral("claude");
        options.refinementProviderId = QStringLiteral("none");
        options.destination = TranscriptDestination::None;
        queue.setOptions(options);
        QVERIFY(queue.start());

        ManifestUpdaterTestAccess::setState(*updater, UpdateController::State::ReadyToRestart);
        QSignalSpy restartAttempts(updater, &UpdateController::openReleasePageRequested);
        updater->installAndRestart();
        QTRY_COMPARE(queue.step(), TranscribeQueue::Step::Export);
        QVERIFY(!queue.results().first().failed());
        QCOMPARE(updater->restartBlocker(), UpdateController::RestartBlocker::UnsavedTranscripts);
        QCOMPARE(updater->state(), UpdateController::State::RestartPending);
        QVERIFY(restartAttempts.isEmpty());

        QuickTranscription transcription(&controller);
        if (release == QStringLiteral("export-one")) {
            transcription.exportResult(0, QStringLiteral("text"),
                                       QUrl::fromLocalFile(dir.filePath(QStringLiteral("silence.txt"))));
        } else if (release == QStringLiteral("export-all")) {
            transcription.exportAll(QStringLiteral("text"), QUrl::fromLocalFile(dir.path()));
        } else {
            transcription.reset();
        }
        QCOMPARE(restartAttempts.count(), 1);
    }
#endif

    // The token an earlier release wrote before restarting into this one
    // reopens the main window, once.
    void anUpdateRelaunchReopensTheMainWindowOnce()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = true; return true; });
        controller.settings()->raw().setValue(SettingsKeys::UpdatesRestoreState, QStringLiteral("listening,settings"));
        controller.settings()->raw().setValue(SettingsKeys::UpdatesRestoreStateTime,
                                              QDateTime::currentMSecsSinceEpoch());

        controller.reopenMainWindowAfterUpdate();
        controller.reopenMainWindowAfterUpdate();
        QTRY_COMPARE(frontEnd.calls, QStringList({QStringLiteral("showMainWindow")}));
        QCoreApplication::processEvents();
        QCOMPARE(frontEnd.calls.size(), 1);
    }

    void filesOpenedBeforeSetupOpenOnceItCompletes()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        FakeAppFrontEnd frontEnd;
        controller.setFrontEnd(&frontEnd);
        controller.settings()->edit([](AppSettings &draft) { draft.setupCompleted = false; return true; });

        controller.showTranscribeFiles({QStringLiteral("/a.wav")});
        controller.ipcCommands()->handle({IpcCommand::Transcribe, {}, {QStringLiteral("/b.wav")}}, nullptr);
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
