#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"

using namespace speecher::test;

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
        return m_candidates;
    }

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


class SingleInstanceIpcTests : public QObject {
    Q_OBJECT

private slots:
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
                [&ipc](const QString &, const QString &, QLocalSocket *socket) {
                    ipc.writeResponse(socket, {true, QStringLiteral("idle"), {}});
                });

        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(500));
        socket.write(QByteArrayLiteral("{\"command\":\"stop\"}\n"));
        QVERIFY(socket.waitForBytesWritten(500));
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
