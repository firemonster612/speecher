#include "common/test_suites.h"
#include "app/CommandLine.h"
#include "app/ProvidersCommand.h"
#include "app/SingleInstanceIpc.h"
#include "core/SettingsStore.h"
#include "platform/PopupPositioner.h"
#include "platform/PopupSurface.h"
#include "recording/RecordingPresentation.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <iostream>
#include <memory>
#include <sstream>
#ifdef Q_OS_WIN
#include <QSemaphore>
#include <functional>
#endif

#include <iostream>
#include <sstream>

using namespace speecher;

class FakeSingleInstancePlatform final : public SingleInstancePlatform {
public:
    FakeSingleInstancePlatform(QString listenName, QStringList candidates = {}, QString detachedPath = {})
        : m_listenName(std::move(listenName))
        , m_candidates(candidates.isEmpty() ? QStringList{m_listenName} : std::move(candidates))
        , m_detachedPath(detachedPath.isEmpty() ? QCoreApplication::applicationFilePath() : std::move(detachedPath))
    {
    }

    QString ipcListenName() const override
    {
        return m_listenName;
    }

    QStringList ipcConnectCandidates() const override
    {
#ifdef Q_OS_WIN
        if (beforeCandidates) beforeCandidates();
#endif
        return m_candidates;
    }
#ifdef Q_OS_WIN
    std::function<void()> beforeCandidates;
#endif

    QString detachedExecutablePath() const override
    {
        return m_detachedPath;
    }

private:
    QString m_listenName;
    QStringList m_candidates;
    QString m_detachedPath;
};

class FakePopupPositioner final : public PopupPositioner {
public:
    explicit FakePopupPositioner(QObject *parent = nullptr)
        : PopupPositioner(parent)
    {
    }

    void positionBottomCenter(PopupSurface &) override
    {
    }
};

static QString uniqueIpcName(const QString &suffix = {})
{
    // Keep names short: on macOS the socket lives under the deep $TMPDIR and
    // the whole path must fit sun_path's 104 bytes.
    QString name = QStringLiteral("spchr-t-%1")
                       .arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    if (!suffix.isEmpty()) {
        name += QStringLiteral("-") + suffix;
    }
    return name;
}

// Runs decision as the command line does, on a thread of its own as a
// separate process would, so this thread can answer for the instance. What
// it prints lands in out and err, read once it finishes, and its exit
// status in exitCode.
static std::unique_ptr<QThread> startCommandLine(const CommandLineDecision &decision,
                                                 const std::shared_ptr<const SingleInstancePlatform> &platform,
                                                 std::ostringstream &out,
                                                 std::ostringstream &err,
                                                 int &exitCode)
{
    std::unique_ptr<QThread> thread(QThread::create([decision, platform, &out, &err, &exitCode] {
        std::streambuf *const stdoutBuffer = std::cout.rdbuf(out.rdbuf());
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(err.rdbuf());
        exitCode = runCliCommand(decision, platform);
        std::cout.rdbuf(stdoutBuffer);
        std::cerr.rdbuf(stderrBuffer);
    }));
    thread->start();
    return thread;
}

static CommandLineDecision statusDecision(const QString &command, bool json)
{
    CommandLineDecision decision;
    decision.mode = LaunchMode::RunCli;
    decision.ipcCommand = command;
    decision.json = json;
    return decision;
}

static QString stateOfLine(const QByteArray &line)
{
    return QJsonDocument::fromJson(line).object().value(QStringLiteral("state")).toString();
}

class SingleInstanceIpcTests : public QObject {
    Q_OBJECT

private slots:
#ifdef Q_OS_WIN
    void simultaneousStartupElectsOneOwner()
    {
        const QString name = uniqueIpcName();
        auto firstPlatform = std::make_shared<FakeSingleInstancePlatform>(name);
        auto secondPlatform = std::make_shared<FakeSingleInstancePlatform>(name);
        QSemaphore firstChecked, continueFirst, firstDone, secondDone, exitThreads;
        bool firstListening = false;
        bool secondListening = false;
        firstPlatform->beforeCandidates = [&] {
            // Stop between the connection probe and creation of the pipe.
            firstChecked.release();
            continueFirst.acquire();
        };
        std::unique_ptr<QThread> first(QThread::create([&] {
            SingleInstanceIpc ipc(firstPlatform);
            firstListening = ipc.listen();
            firstDone.release();
            exitThreads.acquire();
        }));
        std::unique_ptr<QThread> second(QThread::create([&] {
            SingleInstanceIpc ipc(secondPlatform);
            secondListening = ipc.listen();
            secondDone.release();
            exitThreads.acquire();
        }));
        const auto cleanup = qScopeGuard([&] {
            continueFirst.release();
            exitThreads.release(2);
            first->wait();
            second->wait();
        });
        first->start();
        QVERIFY(firstChecked.tryAcquire(1, 5000));
        second->start();
        QVERIFY(secondDone.tryAcquire(1, 5000));
        continueFirst.release();
        QVERIFY(firstDone.tryAcquire(1, 5000));
        QCOMPARE(int(firstListening) + int(secondListening), 1);
    }
#endif

    void singleInstanceIpcDoesNotStealLiveSocket()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        QLocalServer existing;
        QVERIFY(existing.listen(name));

        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc second(platform);
        QString error;
        QVERIFY(!second.listen(&error));
        QVERIFY(error.contains(name));

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));

        existing.close();
        QLocalServer::removeServer(name);
    }

    void singleInstanceIpcRefusesActiveLegacyCandidate()
    {
        const QString listenName = uniqueIpcName(QStringLiteral("stable"));
        const QString legacyName = uniqueIpcName(QStringLiteral("legacy"));
        QLocalServer::removeServer(listenName);
        QLocalServer::removeServer(legacyName);
        QLocalServer existing;
        QVERIFY(existing.listen(legacyName));

        const auto platform = std::make_shared<FakeSingleInstancePlatform>(
            listenName,
            QStringList{listenName, legacyName});
        SingleInstanceIpc second(platform);
        QString error;
        QVERIFY(!second.listen(&error));
        QVERIFY(error.contains(legacyName));

        QLocalSocket socket;
        socket.connectToServer(legacyName);
        QVERIFY(socket.waitForConnected(500));

        existing.close();
        QLocalServer::removeServer(listenName);
        QLocalServer::removeServer(legacyName);
    }

    void singleInstanceIpcRemovesStaleSocketFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString name = dir.filePath(QStringLiteral("stale.sock"));
        QFile stale(name);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        QVERIFY(QFileInfo::exists(name));

        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QString error;
        QVERIFY2(ipc.listen(&error), qPrintable(error));

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));

        QLocalServer::removeServer(name);
    }

    void singleInstanceIpcReportsConnectedServerWithoutResponse()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        QLocalServer existing;
        QVERIFY(existing.listen(name));

        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        IpcResponse response;
        QString error;
        const IpcCommandResult result = SingleInstanceIpc::sendCommandDetailed(QStringLiteral("toggle"),
                                                                               &response,
                                                                               75,
                                                                               platform,
                                                                               &error);
        QCOMPARE(result, IpcCommandResult::NoResponse);
        QVERIFY(error.contains(QStringLiteral("did not respond")));

        existing.close();
        QLocalServer::removeServer(name);
    }

    void singleInstanceIpcBuffersFragmentedRequests()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));
        socket.write(QByteArrayLiteral("{\"command\":\"tog"));
        socket.flush();
        QCoreApplication::processEvents();
        QCOMPARE(commands.count(), 0);

        socket.write(QByteArrayLiteral("gle\"}\n"));
        socket.flush();
        QTRY_COMPARE(commands.count(), 1);
        QCOMPARE(commands.first().at(0).toString(), QStringLiteral("toggle"));
    }

    void singleInstanceIpcReadsTwoFramedCommands()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));
        socket.write(QByteArrayLiteral("{\"command\":\"start\"}\n{\"command\":\"stop\"}\n"));
        socket.flush();
        QTRY_COMPARE(commands.count(), 2);
        QCOMPARE(commands.at(0).at(0).toString(), QStringLiteral("start"));
        QCOMPARE(commands.at(1).at(0).toString(), QStringLiteral("stop"));
    }

    void singleInstanceIpcCarriesTheFilesToTranscribe()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [](const QString &, const QString &, QLocalSocket *socket, const QStringList &) {
                    SingleInstanceIpc::writeResponse(socket, {true, QStringLiteral("idle"), {}});
                });
        const QStringList files{QStringLiteral("/tmp/a.wav"), QStringLiteral("/tmp/b c.mp3")};

        QThread *client = QThread::create([platform, files] {
            SingleInstanceIpc::sendCommandDetailed(
                QStringLiteral("transcribe"), SessionOverrides(), files, nullptr, 2000, platform);
        });
        client->start();
        QTRY_COMPARE(commands.count(), 1);
        client->wait();
        delete client;
        QCOMPARE(commands.first().at(0).toString(), QStringLiteral("transcribe"));
        QCOMPARE(commands.first().at(3).toStringList(), files);
    }

    void singleInstanceIpcCarriesTheSessionOverrides()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [](const QString &, const QString &, QLocalSocket *socket) {
                    SingleInstanceIpc::writeResponse(socket, {true, QStringLiteral("idle"), {}});
                });

        QThread *client = QThread::create([platform] {
            SingleInstanceIpc::sendCommandDetailed(QStringLiteral("toggle"),
                                                   {OutputFormat::Html, QStringLiteral("ai_coding"),
                                                    QStringLiteral("de")},
                                                   nullptr,
                                                   2000,
                                                   platform);
        });
        client->start();
        QTRY_COMPARE(commands.count(), 1);
        client->wait();
        delete client;
        QCOMPARE(commands.first().at(1).toString(), QStringLiteral("html"));
        QCOMPARE(commands.first().at(4).toString(), QStringLiteral("ai_coding"));
        QCOMPARE(commands.first().at(5).toString(), QStringLiteral("de"));
    }

    // record start hands over its file and terms; every record command's answer
    // carries the recording, which record status prints.
    void recordCommandsCarryTheRecordingBothWays()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);
        const RecordingStatus reconnecting{
            true, QStringLiteral("/tmp/call.md"), 3723000,
            {{QStringLiteral("me"), RecordingStream::State::Reconnecting, QStringLiteral("Connection reset"), 2000}}};
        const RecordingStatus stoppedIncomplete{true,
                                                QStringLiteral("/tmp/call.md"),
                                                3723000,
                                                {{QStringLiteral("me"), RecordingStream::State::Stopped}},
                                                2,
                                                QStringLiteral("Could not write /tmp/call.md: No space left on device")};
        const RecordingStatus started{true,
                                      QStringLiteral("/tmp/call.md"),
                                      0,
                                      {{QStringLiteral("me"), RecordingStream::State::Paused},
                                       {QStringLiteral("them"), RecordingStream::State::Stopped,
                                        QStringLiteral("No default output.")}},
                                      0,
                                      {},
                                      echoCancellationNotBuiltText()};
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&](const QString &command, const QString &, QLocalSocket *socket) {
                    IpcResponse reply{true, QStringLiteral("idle"), {}};
                    reply.recording = command == QStringLiteral("recordStatus") ? reconnecting
                        : command == QStringLiteral("recordStop")               ? stoppedIncomplete
                                                                                : started;
                    SingleInstanceIpc::writeResponse(socket, reply);
                });

        IpcResponse response;
        QThread *client = QThread::create([platform, &response] {
            SingleInstanceIpc::sendRecordStart(QStringLiteral("/tmp/call.md"), {QStringLiteral("readSharedChoice")},
                                               true, &response, 2000, platform);
        });
        client->start();
        QTRY_VERIFY(client->isFinished());
        delete client;
        QCOMPARE(commands.first().at(0).toString(), QStringLiteral("recordStart"));
        QCOMPARE(commands.first().at(3).toStringList(), QStringList{QStringLiteral("/tmp/call.md")});
        QCOMPARE(commands.first().at(6).toStringList(), QStringList{QStringLiteral("readSharedChoice")});
        QVERIFY(commands.first().at(7).toBool());
        QVERIFY(response.recording && response.recording->recording);

        std::ostringstream out;
        std::ostringstream err;
        // Runs a record command as the command line does; returns its exit
        // status, with what it printed in out and err.
        const auto run = [&](const QString &command, bool json) {
            CommandLineDecision decision;
            decision.mode = LaunchMode::RunCli;
            decision.ipcCommand = command;
            decision.json = json;
            out.str({});
            err.str({});
            int exitCode = -1;
            QThread *runner = QThread::create([&] {
                std::streambuf *const stdoutBuffer = std::cout.rdbuf(out.rdbuf());
                std::streambuf *const stderrBuffer = std::cerr.rdbuf(err.rdbuf());
                exitCode = runCliCommand(decision, platform);
                std::cout.rdbuf(stdoutBuffer);
                std::cerr.rdbuf(stderrBuffer);
            });
            runner->start();
            // The instance answers on this thread, so wait with its events.
            QTest::qWaitFor([runner] { return runner->isFinished(); }, 5000);
            runner->wait();
            delete runner;
            return exitCode;
        };
        // A start that runs with a problem still succeeds, and says what on
        // stderr: a system audio stream that failed, echo cancellation off.
        QCOMPARE(run(QStringLiteral("recordStart"), false), 0);
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral("/tmp/call.md\n"));
        QCOMPARE(QString::fromStdString(err.str()),
                 QStringLiteral("The system audio stream stopped: No default output.\n") + echoCancellationNotBuiltText()
                     + QLatin1Char('\n'));
        QCOMPARE(recordingStatusText(started),
                 QStringLiteral("path: /tmp/call.md\nduration: 00:00:00\nmicrophone: paused\n"
                                "system audio: stopped, No default output.\nwarning: ")
                     + echoCancellationNotBuiltText());
        QCOMPARE(statusWatchText(QStringLiteral("Listening"), started),
                 QStringLiteral("Listening, recording 00:00:00, microphone paused, system audio stopped, echo "
                                "cancellation off"));
        const QJsonObject startedJson = recordingStatusJson(started);
        QCOMPARE(startedJson.value(QStringLiteral("echoCancellationWarning")).toString(),
                 echoCancellationNotBuiltText());
        QCOMPARE(recordingStatusFromJson(startedJson).streams.first().state, RecordingStream::State::Paused);
        QCOMPARE(recordingStatusFromJson(startedJson).echoCancellationWarning, echoCancellationNotBuiltText());

        QCOMPARE(run(QStringLiteral("recordStatus"), true), 0);
        QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(out.str())).object(),
                 QJsonObject({{QStringLiteral("recording"), true},
                              {QStringLiteral("path"), QStringLiteral("/tmp/call.md")},
                              {QStringLiteral("durationMs"), 3723000},
                              {QStringLiteral("streams"),
                               QJsonArray{QJsonObject{{QStringLiteral("speaker"), QStringLiteral("me")},
                                                      {QStringLiteral("state"), QStringLiteral("reconnecting")},
                                                      {QStringLiteral("problem"), QStringLiteral("Connection reset")},
                                                      {QStringLiteral("lostAudioMs"), 2000}}}}}));
        // The problem goes to stderr too, for a script that reads only stdout.
        QCOMPARE(QString::fromStdString(err.str()),
                 QStringLiteral("The microphone stream is reconnecting: Connection reset\n"
                                "The microphone stream was down so long that the oldest 00:00:02 of audio waiting for it "
                                "was dropped.\n"));

        QCOMPARE(run(QStringLiteral("recordStatus"), false), 0);
        QCOMPARE(QString::fromStdString(out.str()),
                 QStringLiteral("path: /tmp/call.md\nduration: 01:02:03\n"
                                "microphone: reconnecting, Connection reset, lost 00:00:02 of audio\n"));

        // A recording whose file missed lines still prints the file, says
        // what it missed, and fails.
        QCOMPARE(run(QStringLiteral("recordStop"), false), 1);
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral("/tmp/call.md\n"));
        QCOMPARE(QString::fromStdString(err.str()),
                 QStringLiteral("Could not write /tmp/call.md: No space left on device (2 lines missing)\n"));
        QCOMPARE(recordingStatusFromJson(recordingStatusJson(stoppedIncomplete)).writeError,
                 stoppedIncomplete.writeError);
    }

    void theCommandLineFailsWhenTheInstanceIgnoresTheProfile_data()
    {
        QTest::addColumn<bool>("echoes");
        QTest::newRow("current instance") << true;
        QTest::newRow("instance older than --profile") << false;
    }

    void theCommandLineFailsWhenTheInstanceIgnoresTheProfile()
    {
        QFETCH(bool, echoes);
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [echoes](const QString &, const QString &, QLocalSocket *socket, const QStringList &,
                         const QString &writingProfile) {
                    IpcResponse reply{true, QStringLiteral("starting"), {}};
                    if (echoes) {
                        reply.writingProfile = writingProfile;
                    }
                    SingleInstanceIpc::writeResponse(socket, reply);
                });
        CommandLineDecision decision;
        decision.mode = LaunchMode::RunCli;
        decision.ipcCommand = QStringLiteral("toggle");
        decision.sessionOverrides.writingProfile = QStringLiteral("work");

        int exitCode = -1;
        QThread *client = QThread::create([&exitCode, &decision, platform] {
            exitCode = runCliCommand(decision, platform);
        });
        client->start();
        QTRY_VERIFY(client->isFinished());
        delete client;
        QCOMPARE(exitCode, echoes ? 0 : 1);
    }

    void lastPrintsTheRunningInstancesTranscript_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("exitCode");
        QTest::newRow("a transcript") << QStringLiteral("Ship it on Friday.") << QString() << 0;
        QTest::newRow("no transcript yet") << QString() << QString() << 1;
        QTest::newRow("instance older than last") << QString() << kUnknownIpcCommandMessage << 1;
    }

    void lastPrintsTheRunningInstancesTranscript()
    {
        QFETCH(QString, text);
        QFETCH(QString, message);
        QFETCH(int, exitCode);
        IpcResponse reply{!text.isEmpty(), QStringLiteral("idle"), message};
        reply.text = text;
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [reply](const QString &, const QString &, QLocalSocket *socket) {
                    SingleInstanceIpc::writeResponse(socket, reply);
                });
        CommandLineDecision decision;
        decision.mode = LaunchMode::RunCli;
        decision.ipcCommand = QStringLiteral("last");

        std::ostringstream out;
        std::ostringstream err;
        int lastExitCode = -1;
        {
            std::streambuf *const stdoutBuffer = std::cout.rdbuf(out.rdbuf());
            std::streambuf *const stderrBuffer = std::cerr.rdbuf(err.rdbuf());
            const auto restoreStreams = qScopeGuard([stdoutBuffer, stderrBuffer] {
                std::cout.rdbuf(stdoutBuffer);
                std::cerr.rdbuf(stderrBuffer);
            });
            QThread *client = QThread::create([&lastExitCode, &decision, platform] {
                lastExitCode = runCliCommand(decision, platform);
            });
            client->start();
            QTRY_VERIFY(client->isFinished());
            delete client;
        }
        QCOMPARE(lastExitCode, exitCode);
        QCOMPARE(QString::fromStdString(out.str()), text.isEmpty() ? QString() : text + QLatin1Char('\n'));
        QCOMPARE(QString::fromStdString(err.str()).contains(QStringLiteral("older")), !message.isEmpty());
    }

    void providersAsksTheRunningApp_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("answered");
        QTest::newRow("the app's reports") << QString() << true;
        QTest::newRow("app older than providers") << kUnknownIpcCommandMessage << false;
    }

    // The running app's reports arrive as it judged them. One that doesn't
    // know the command leaves them to this process without a word.
    void providersAsksTheRunningApp()
    {
        QFETCH(QString, message);
        QFETCH(bool, answered);
        const QList<ProviderReport> sent{
            {QStringLiteral("codex"), ProviderRole::Speech, QStringLiteral("ChatGPT Codex"), true, true, false, false,
             QStringLiteral("Not signed in to ChatGPT.")},
            {QStringLiteral("local"), ProviderRole::Refinement, QStringLiteral("Local Runner"), false, true,
             std::nullopt, true, {}},
        };
        IpcResponse reply{true, QStringLiteral("idle"), message};
        if (answered) {
            reply.text = QString::fromUtf8(providerReportsJson(sent));
        }
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QString command;
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [reply, &command](const QString &received, const QString &, QLocalSocket *socket) {
                    command = received;
                    SingleInstanceIpc::writeResponse(socket, reply);
                });

        std::optional<QList<ProviderReport>> reports;
        QString error;
        QThread *client = QThread::create([&reports, &error, platform] {
            reports = runningAppProviderReports(platform, &error);
        });
        client->start();
        QTRY_VERIFY(client->isFinished());
        delete client;
        QCOMPARE(command, QStringLiteral("providers"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(reports.has_value(), answered);
        if (answered) {
            QCOMPARE(providerReportsJson(*reports), providerReportsJson(sent));
        }
    }

    void providersFallsBackWithoutARunningApp()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        QString error;
        QVERIFY(!runningAppProviderReports(std::make_shared<FakeSingleInstancePlatform>(name), &error));
        QVERIFY(error.isEmpty());
    }

    void vocabularyAddSendsTheTermsToTheRunningInstance_data()
    {
        QTest::addColumn<bool>("saved");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("exitCode");
        QTest::addColumn<QString>("stderrText");
        QTest::newRow("one already listed")
            << true << QString() << 0 << QStringLiteral("Already in the vocabulary: kwin\n");
        QTest::newRow("save failed")
            << false << QString() << 1 << QStringLiteral("Could not save the vocabulary to Speecher's settings.\n");
        QTest::newRow("instance older than vocabulary add")
            << false << kUnknownIpcCommandMessage << 1
            << QStringLiteral("The running Speecher is older and doesn't know `vocabulary add`. Quit it with "
                              "`speecher quit` and run the command again.\n");
    }

    void vocabularyAddSendsTheTermsToTheRunningInstance()
    {
        QFETCH(bool, saved);
        QFETCH(QString, message);
        QFETCH(int, exitCode);
        QFETCH(QString, stderrText);
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QStringList received;
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&received, saved, message](const QString &, const QString &, QLocalSocket *socket,
                                            const QStringList &, const QString &, const QString &,
                                            const QStringList &terms) {
                    received = terms;
                    IpcResponse reply{saved, QStringLiteral("idle"), message};
                    if (saved) {
                        reply.skippedTerms = {QStringLiteral("kwin")};
                    }
                    SingleInstanceIpc::writeResponse(socket, reply);
                });
        CommandLineDecision decision;
        decision.mode = LaunchMode::RunCli;
        decision.ipcCommand = QStringLiteral("addVocabulary");
        decision.vocabularyTerms = {QStringLiteral("FileTranscriptionSession"), QStringLiteral("kwin")};

        std::ostringstream out;
        std::ostringstream err;
        int addExitCode = -1;
        {
            std::streambuf *const stdoutBuffer = std::cout.rdbuf(out.rdbuf());
            std::streambuf *const stderrBuffer = std::cerr.rdbuf(err.rdbuf());
            const auto restoreStreams = qScopeGuard([stdoutBuffer, stderrBuffer] {
                std::cout.rdbuf(stdoutBuffer);
                std::cerr.rdbuf(stderrBuffer);
            });
            QThread *client = QThread::create([&addExitCode, &decision, platform] {
                addExitCode = runCliCommand(decision, platform);
            });
            client->start();
            QTRY_VERIFY(client->isFinished());
            delete client;
        }
        QCOMPARE(received, decision.vocabularyTerms);
        QCOMPARE(addExitCode, exitCode);
        QCOMPARE(QString::fromStdString(out.str()), QString());
        QCOMPARE(QString::fromStdString(err.str()), stderrText);
    }

    // With no running instance, vocabulary add saves the terms itself.
    void vocabularyAddWithoutAnInstanceSavesTheTerms()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.setCustomVocabulary({QStringLiteral("KWin")});
        settings.raw().sync();
        CommandLineDecision decision;
        decision.mode = LaunchMode::RunCli;
        decision.ipcCommand = QStringLiteral("addVocabulary");
        decision.vocabularyTerms = {QStringLiteral("FileTranscriptionSession"), QStringLiteral("kwin")};

        std::ostringstream err;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(err.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        QCOMPARE(runCliCommand(decision, std::make_shared<FakeSingleInstancePlatform>(uniqueIpcName())), 0);
        QCOMPARE(QString::fromStdString(err.str()), QStringLiteral("Already in the vocabulary: kwin\n"));
        settings.raw().sync();
        QCOMPARE(settings.customVocabulary(),
                 QStringList({QStringLiteral("FileTranscriptionSession"), QStringLiteral("KWin")}));
    }

    // A settings file that cannot be written fails the command.
    void vocabularyAddWithoutAnInstanceReportsAFailedSave()
    {
#ifdef Q_OS_WIN
        QSKIP("Windows keeps the settings in the registry");
#endif
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().sync();
        const QString path = settings.raw().fileName();
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        const auto removeDirectory = qScopeGuard([path] { QDir().rmdir(path); });
        CommandLineDecision decision;
        decision.mode = LaunchMode::RunCli;
        decision.ipcCommand = QStringLiteral("addVocabulary");
        decision.vocabularyTerms = {QStringLiteral("FileTranscriptionSession")};

        std::ostringstream err;
        std::streambuf *const stderrBuffer = std::cerr.rdbuf(err.rdbuf());
        const auto restoreStderr = qScopeGuard([stderrBuffer] { std::cerr.rdbuf(stderrBuffer); });
        QCOMPARE(runCliCommand(decision, std::make_shared<FakeSingleInstancePlatform>(uniqueIpcName())), 1);
        QCOMPARE(QString::fromStdString(err.str()),
                 QStringLiteral("Could not save the vocabulary to Speecher's settings.\n"));
    }

    // Watchers hold their sockets open, more of them than the accept cap,
    // and every other command is still answered.
    void statusWatchersHearEveryChangeWithoutTakingAcceptSlots()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&ipc](const QString &command, const QString &, QLocalSocket *socket) {
                    if (command == QStringLiteral("watchStatus")) {
                        ipc.addStatusWatcher(socket, {true, QStringLiteral("idle"), {}});
                    } else {
                        SingleInstanceIpc::writeResponse(socket, {true, QStringLiteral("idle"), {}});
                    }
                });

        // Each connects once the one before has its answer. Nine connecting
        // at once would all be unanswered together, and Windows accepts
        // them in one go, so the cap turns the ninth away (status --watch
        // tries again) before any becomes a watcher.
        QLocalSocket watchers[9];
        for (QLocalSocket &watcher : watchers) {
            watcher.connectToServer(name);
            QVERIFY(watcher.waitForConnected(500));
            watcher.write(QByteArrayLiteral("{\"command\":\"watchStatus\"}\n"));
            watcher.flush();
            QTRY_VERIFY(watcher.canReadLine());
            QCOMPARE(stateOfLine(watcher.readLine()), QStringLiteral("idle"));
        }
        ipc.publishStatus({true, QStringLiteral("listening"), {}});
        for (QLocalSocket &watcher : watchers) {
            QTRY_VERIFY(watcher.canReadLine());
            QCOMPARE(stateOfLine(watcher.readLine()), QStringLiteral("listening"));
        }

        // One that leaves takes nothing from the rest.
        watchers[0].disconnectFromServer();
        QTest::qWait(50);
        ipc.publishStatus({true, QStringLiteral("refining"), {}});
        for (qsizetype index = 1; index < qsizetype(std::size(watchers)); ++index) {
            QTRY_VERIFY(watchers[index].canReadLine());
            QCOMPARE(stateOfLine(watchers[index].readLine()), QStringLiteral("refining"));
        }

        IpcCommandResult result = IpcCommandResult::Unavailable;
        std::unique_ptr<QThread> client(QThread::create([platform, &result] {
            result = SingleInstanceIpc::sendCommandDetailed(QStringLiteral("status"), nullptr, 2000, platform);
        }));
        client->start();
        QVERIFY(QTest::qWaitFor([&client] { return client->isFinished(); }, 5000));
        QCOMPARE(result, IpcCommandResult::Sent);
    }

    // A watcher that stops reading is dropped once its unread lines pile up,
    // rather than kept and buffered for.
    void aStatusWatcherThatStopsReadingIsDropped()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&ipc](const QString &, const QString &, QLocalSocket *socket) {
                    ipc.addStatusWatcher(socket, {true, QStringLiteral("idle"), {}});
                });

        QLocalSocket stalled;
        // Qt reads no more of the socket than this, so the rest waits in the
        // server's buffer.
        stalled.setReadBufferSize(1);
        stalled.connectToServer(name);
        QVERIFY(stalled.waitForConnected(500));
        stalled.write(QByteArrayLiteral("{\"command\":\"watchStatus\"}\n"));
        stalled.flush();
        QTRY_COMPARE(commands.count(), 1);

        IpcResponse large{true, QStringLiteral("listening"), {}};
        large.text = QString(16 * 1024, QLatin1Char('x'));
        constexpr int publishes = 200;
        for (int index = 0; index < publishes; ++index) {
            ipc.publishStatus(large);
        }
        stalled.setReadBufferSize(0);
        qint64 received = 0;
        QVERIFY(QTest::qWaitFor([&stalled, &received] {
            received += stalled.readAll().size();
            return stalled.state() == QLocalSocket::UnconnectedState;
        }, 5000));
        received += stalled.readAll().size();
        QVERIFY2(received < publishes * large.text.size(), qPrintable(QString::number(received)));
    }

    void statusWatchPrintsALineForEachChange_data()
    {
        QTest::addColumn<bool>("json");
        QTest::addColumn<QString>("printed");
        QTest::newRow("text") << false
                              << QStringLiteral("idle\n"
                                                "recording 01:02:03, microphone reconnecting\n"
                                                "listening, recording 01:02:03, microphone reconnecting\n");
        const QString recording = QStringLiteral(
            R"("recording":{"durationMs":3723000,"path":"/tmp/call.md","recording":true,)"
            R"("streams":[{"lostAudioMs":2000,"problem":"Connection reset","speaker":"me","state":"reconnecting"}]})");
        QTest::newRow("json") << true
                              << QStringLiteral(R"({"recording":false,"state":"idle"})"
                                                "\n{%1,\"state\":\"idle\"}\n{%1,\"state\":\"listening\"}\n")
                                     .arg(recording);
    }

    // Started before Speecher, the watch prints the idle status, picks the
    // app up once it starts, prints each change it writes, and ends with
    // status 0 when the app quits.
    void statusWatchPrintsALineForEachChange()
    {
        QFETCH(bool, json);
        QFETCH(QString, printed);
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        std::ostringstream out;
        std::ostringstream err;
        int exitCode = -1;
        std::unique_ptr<QThread> cli =
            startCommandLine(statusDecision(QStringLiteral("watchStatus"), json), platform, out, err, exitCode);
        // Past the first look for an app, which finds none.
        QTest::qWait(300);

        auto ipc = std::make_unique<SingleInstanceIpc>(platform);
        QVERIFY(ipc->listen());
        QStringList commands;
        connect(ipc.get(), &SingleInstanceIpc::commandReceived, ipc.get(),
                [&ipc, &commands](const QString &command, const QString &, QLocalSocket *socket) {
                    commands << command;
                    ipc->addStatusWatcher(socket, {true, QStringLiteral("idle"), {}});
                });
        QTRY_COMPARE_WITH_TIMEOUT(commands, QStringList{QStringLiteral("watchStatus")}, 5000);
        IpcResponse recording{true, QStringLiteral("idle"), {}};
        recording.recording = RecordingStatus{
            true, QStringLiteral("/tmp/call.md"), 3723000,
            {{QStringLiteral("me"), RecordingStream::State::Reconnecting, QStringLiteral("Connection reset"), 2000}}};
        ipc->publishStatus(recording);
        recording.state = QStringLiteral("listening");
        ipc->publishStatus(recording);
        QCoreApplication::processEvents();
        ipc.reset();

        QVERIFY(QTest::qWaitFor([&cli] { return cli->isFinished(); }, 5000));
        QCOMPARE(exitCode, 0);
        QCOMPARE(QString::fromStdString(out.str()), printed);
        QCOMPARE(QString::fromStdString(err.str()), QString());
    }

    // A Speecher that takes the connection but closes it unanswered, as one
    // starting, quitting or with every slot taken does, is tried again.
    void statusWatchRetriesASpeecherThatHasNotAnswered()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        auto ipc = std::make_unique<SingleInstanceIpc>(platform);
        QVERIFY(ipc->listen());
        int watches = 0;
        connect(ipc.get(), &SingleInstanceIpc::commandReceived, ipc.get(),
                [&ipc, &watches](const QString &, const QString &, QLocalSocket *socket) {
                    if (++watches == 1) {
                        socket->disconnectFromServer();
                    } else {
                        ipc->addStatusWatcher(socket, {true, QStringLiteral("listening"), {}});
                    }
                });
        std::ostringstream out;
        std::ostringstream err;
        int exitCode = -1;
        std::unique_ptr<QThread> cli =
            startCommandLine(statusDecision(QStringLiteral("watchStatus"), false), platform, out, err, exitCode);
        QTRY_COMPARE_WITH_TIMEOUT(watches, 2, 5000);
        QCoreApplication::processEvents();
        ipc.reset();

        QVERIFY(QTest::qWaitFor([&cli] { return cli->isFinished(); }, 5000));
        QCOMPARE(exitCode, 0);
        // Running, it is not idle.
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral("listening\n"));
        QCOMPARE(QString::fromStdString(err.str()), QString());
    }

    // A watch whose reader has gone, as `| head -1`'s does, ends with status
    // 0 at its next line and leaves the Speecher's watcher slot.
    void statusWatchEndsWhenStdoutFails()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QPointer<QLocalSocket> watcher;
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&ipc, &watcher](const QString &, const QString &, QLocalSocket *socket) {
                    watcher = socket;
                    ipc.addStatusWatcher(socket, {true, QStringLiteral("idle"), {}});
                });
        int exitCode = -1;
        std::unique_ptr<QThread> cli(QThread::create([platform, &exitCode] {
            // No buffer: every write fails, as one to a closed pipe does.
            std::streambuf *const stdoutBuffer = std::cout.rdbuf(nullptr);
            exitCode = runCliCommand(statusDecision(QStringLiteral("watchStatus"), false), platform);
            std::cout.rdbuf(stdoutBuffer);
        }));
        cli->start();
        QVERIFY(QTest::qWaitFor([&cli] { return cli->isFinished(); }, 5000));
        QCOMPARE(exitCode, 0);
        QTRY_VERIFY(!watcher || watcher->state() == QLocalSocket::UnconnectedState);
    }

    // The start of a request sent with watchStatus is not one, so it does
    // not expire the watcher as an incomplete request would.
    void aStatusWatcherKeepsWatchingPastAPartialRequest()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        SingleInstanceIpc ipc(std::make_shared<FakeSingleInstancePlatform>(name));
        QVERIFY(ipc.listen());
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&ipc](const QString &, const QString &, QLocalSocket *socket) {
                    ipc.addStatusWatcher(socket, {true, QStringLiteral("idle"), {}});
                });
        QLocalSocket watcher;
        watcher.connectToServer(name);
        QVERIFY(watcher.waitForConnected(500));
        watcher.write(QByteArrayLiteral("{\"command\":\"watchStatus\"}\n{"));
        watcher.flush();
        QTRY_VERIFY(watcher.canReadLine());
        QCOMPARE(stateOfLine(watcher.readLine()), QStringLiteral("idle"));

        // Past the incomplete-request timeout and a sweep.
        QTest::qWait(3000);
        QCOMPARE(watcher.state(), QLocalSocket::ConnectedState);
        ipc.publishStatus({true, QStringLiteral("listening"), {}});
        QTRY_VERIFY(watcher.canReadLine());
        QCOMPARE(stateOfLine(watcher.readLine()), QStringLiteral("listening"));
    }

    // status --json with no Speecher running prints the idle status.
    void statusJsonWithoutSpeecherIsIdle()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        std::ostringstream out;
        std::ostringstream err;
        int exitCode = -1;
        std::unique_ptr<QThread> cli = startCommandLine(statusDecision(QStringLiteral("status"), true),
                                                        std::make_shared<FakeSingleInstancePlatform>(name), out, err,
                                                        exitCode);
        QVERIFY(QTest::qWaitFor([&cli] { return cli->isFinished(); }, 5000));
        QCOMPARE(exitCode, 0);
        QCOMPARE(QString::fromStdString(out.str()), QStringLiteral(R"({"recording":false,"state":"idle"})" "\n"));
    }

    void statusWatchRefusesAnOlderInstance()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [](const QString &, const QString &, QLocalSocket *socket) {
                    SingleInstanceIpc::writeResponse(socket, {false, QStringLiteral("idle"), kUnknownIpcCommandMessage});
                });
        std::ostringstream out;
        std::ostringstream err;
        int exitCode = -1;
        std::unique_ptr<QThread> cli =
            startCommandLine(statusDecision(QStringLiteral("watchStatus"), false), platform, out, err, exitCode);
        QVERIFY(QTest::qWaitFor([&cli] { return cli->isFinished(); }, 5000));
        QCOMPARE(exitCode, 1);
        QCOMPARE(QString::fromStdString(out.str()), QString());
        QCOMPARE(QString::fromStdString(err.str()),
                 QStringLiteral("The running Speecher is older and doesn't know `status --watch`. Quit it with "
                                "`speecher quit` and run the command again.\n"));
    }

    void singleInstanceIpcExpiresIncompleteRequests()
    {
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));
        socket.write(QByteArrayLiteral("{"));
        socket.flush();

        // The server holds the incomplete request briefly, then expires the
        // connection instead of keeping the socket and buffer forever.
        QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QLocalSocket::UnconnectedState, 5000);
        QCOMPARE(commands.count(), 0);
    }

    void singleInstanceIpcExpiresSilentConnectionsBeforeTheyWedgeAdmission()
    {
        // Eight clients that connect and never write a byte fill the accept
        // cap. The deadline armed at accept must expire them so a later
        // legitimate client is still admitted and answered.
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);

        QLocalSocket silent[8];
        for (QLocalSocket &socket : silent) {
            socket.connectToServer(name);
            QVERIFY(socket.waitForConnected(500));
        }
        for (QLocalSocket &socket : silent) {
            QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QLocalSocket::UnconnectedState, 5000);
        }

        QLocalSocket client;
        client.connectToServer(name);
        QVERIFY(client.waitForConnected(500));
        client.write(QByteArrayLiteral("{\"command\":\"toggle\"}\n"));
        client.flush();
        QTRY_COMPARE(commands.count(), 1);
        QCOMPARE(commands.first().at(0).toString(), QStringLiteral("toggle"));
    }

    void singleInstanceIpcSurvivesAClientThatDisconnectsAfterSending()
    {
        // A CLI client can send its command and drop the connection without
        // waiting. The command may then arrive from inside the socket's own
        // dying state change; answering it must not write into the teardown.
        const QString name = uniqueIpcName();
        QLocalServer::removeServer(name);
        const auto platform = std::make_shared<FakeSingleInstancePlatform>(name);
        SingleInstanceIpc ipc(platform);
        QVERIFY(ipc.listen());
        QSignalSpy commands(&ipc, &SingleInstanceIpc::commandReceived);
        connect(&ipc, &SingleInstanceIpc::commandReceived, &ipc,
                [&ipc](const QString &, const QString &, QLocalSocket *socket, const QStringList &) {
                    ipc.writeResponse(socket, {true, QStringLiteral("idle"), {}});
                });

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));
        socket.write(QByteArrayLiteral("{\"command\":\"stop\"}\n"));
        // flush() queues the bytes to the OS; on Windows named pipes the write
        // completes synchronously so waitForBytesWritten would see nothing left.
        socket.flush();
        socket.disconnectFromServer();
        QTRY_COMPARE(commands.count(), 1);
        // Surviving the response write is the assertion; a crash fails the run.
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
    }
};

int runSingleInstanceIpcTests(int argc, char **argv)
{
    SingleInstanceIpcTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_single_instance_ipc.moc"
