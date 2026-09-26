#include "app/LocalSetup.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "common/test_http.h"
#include "common/test_suites.h"
#include "core/LocalModelCatalog.h"
#include "providers/LocalModelStore.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

using namespace speecher;

namespace {

constexpr quint64 gb = 1000ull * 1000 * 1000;
constexpr quint64 gib = 1ull << 30;

HardwareProfile laptop4750u()
{
    // The prototype's "2020 laptop": 16 GB shared with an integrated GPU.
    HardwareProfile hardware;
    hardware.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
    hardware.chipName = QStringLiteral("AMD Ryzen 7 PRO 4750U with Radeon Graphics");
    hardware.systemRamBytes = 16 * gb;
    hardware.availableRamBytes = 12 * gb;
    return hardware;
}

HardwareProfile m4Max36GB()
{
    HardwareProfile hardware;
    hardware.accelerator = HardwareProfile::Accelerator::AppleSilicon;
    hardware.chipName = QStringLiteral("Apple M4 Max");
    hardware.systemRamBytes = 36 * gb;
    hardware.gpuMemoryBytes = 28 * gb;
    return hardware;
}

LocalModel fakeModel(const QByteArray &content)
{
    LocalModel model;
    model.id = QStringLiteral("fake");
    model.huggingFaceRepo = QStringLiteral("owner/repo");
    model.revision = QStringLiteral("abc123");
    model.fileName = QStringLiteral("fake.gguf");
    model.sizeBytes = content.size();
    model.sha256 = QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    return model;
}

// Answers one request with status and body, and hands back the request.
QByteArray serveOnce(QTcpServer &server, const QByteArray &statusLine, const QByteArray &body,
                     qint64 declaredLength = -1)
{
    if (!server.hasPendingConnections() && !server.waitForNewConnection(5000)) {
        return {};
    }
    QTcpSocket *socket = server.nextPendingConnection();
    const QByteArray request = test::readHttpRequest(socket, 2000);
    socket->write("HTTP/1.1 " + statusLine + "\r\nContent-Length: "
                  + QByteArray::number(declaredLength < 0 ? body.size() : declaredLength)
                  + "\r\nConnection: close\r\n\r\n" + body);
    socket->flush();
    socket->waitForBytesWritten(2000);
    socket->disconnectFromHost();
    return request;
}

class LocalModelsTests : public QObject {
    Q_OBJECT

private slots:
    void endpointChecksDiscardSupersededResults_data()
    {
        QTest::addColumn<bool>("refinement");
        QTest::newRow("speech") << false;
        QTest::newRow("refinement") << true;
    }

    void endpointChecksDiscardSupersededResults()
    {
        QFETCH(bool, refinement);
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        QTcpServer slow, fast;
        QVERIFY(slow.listen(QHostAddress::LocalHost));
        QVERIFY(fast.listen(QHostAddress::LocalHost));
        const auto endpoint = [](quint16 port) {
            SpeechEndpointSettings result;
            result.baseUrl = QString("http://127.0.0.1:%1").arg(port);
            return result;
        };
        const auto modelsForCheck = [&] {
            const auto facts = setup.liveFacts();
            return refinement ? facts.refinementEndpointModels : facts.speechEndpointModels;
        };
        const auto check = [&](AppSettings &snapshot) {
            snapshot.refinement.endpoint.baseUrl = snapshot.speech.endpoint.baseUrl;
            settings.applySnapshot(snapshot);
            if (refinement) setup.checkRefinementEndpoint(snapshot.refinement);
            else setup.checkSpeechEndpoint(snapshot.speech.endpoint);
        };
        auto snapshot = settings.snapshot();
        snapshot.speech.endpoint = endpoint(slow.serverPort());
        check(snapshot);
        QTRY_VERIFY(slow.hasPendingConnections());
        snapshot.speech.endpoint = endpoint(fast.serverPort());
        check(snapshot);
        QTRY_VERIFY(fast.hasPendingConnections());
        serveOnce(fast, "200 OK", R"({"data":[{"id":"new-B"}]})");
        QTRY_COMPARE(modelsForCheck(), QStringList{"new-B"});
        serveOnce(slow, "200 OK", R"({"data":[{"id":"old-A"}]})");
        QTest::qWait(100);
        QCOMPARE(modelsForCheck(), QStringList{"new-B"});
        snapshot.speech.endpoint.model = "edited-after-check";
        snapshot.refinement.endpoint.model = "edited-after-check";
        settings.applySnapshot(snapshot);
        const auto facts = setup.liveFacts();
        QVERIFY((refinement ? facts.refinementEndpointStatus : facts.speechEndpointStatus).isEmpty());
        QVERIFY(modelsForCheck().isEmpty());
    }

    void sharedChoicesPreserveSavedConfiguration()
    {
        const QList<DetectedRunner> runners{{"ollama", "Ollama", {}, {}, {"other", "gemma4:e4b:latest"}},
                                             {"lmstudio", "LM Studio", {}, {}, {"studio-model"}}};
        const CleanupModel suggestion{"gemma4:e4b", "Gemma", 100};
        auto choice = resolveRunnerChoice({"lmstudio", "studio-model"}, runners, suggestion);
        QCOMPARE(choice.selection.runner, QString("lmstudio"));
        QCOMPARE(choice.available->id, QString("lmstudio"));
        choice = resolveRunnerChoice({"missing", "saved-model"}, runners, suggestion);
        QVERIFY(!choice.available);
        QCOMPARE(choice.selection.model, QString("saved-model"));
        choice = resolveRunnerChoice({{}, {}}, runners, suggestion);
        QCOMPARE(choice.selection.runner, QString("ollama"));
        QCOMPARE(choice.selection.model, QString("gemma4:e4b:latest"));
        QVERIFY(!choice.offerPull);
        choice = resolveRunnerChoice({"ollama", "saved-model"}, runners, CleanupModel{"absent", {}, 0});
        QVERIFY(choice.offerPull);
        QVERIFY(choice.showSuggestion);
        choice = resolveRunnerChoice({"ollama", "saved-model"}, runners, std::nullopt);
        QVERIFY(choice.showSuggestion);
        QVERIFY(!choice.offerPull);
        for (const auto &id : {"local", "endpoint", "none"})
            QCOMPARE(setupProviderChoice(id, {"claude"}, false), QString(id));
        QCOMPARE(setupProviderChoice("claude", {"codex"}, false), QString("codex"));
        QCOMPARE(setupProviderChoice("claude", {"codex"}, true), QString("claude"));
    }

    void welcomeDefaultsFollowOnlySignInsAndUndoTheirOwnWrite()
    {
        WelcomeChoice choice;
        auto provider = choice.update("claude", {"local", "endpoint"}, false);
        QVERIFY(choice.local());
        QCOMPARE(provider, QString("local"));
        provider = choice.update(provider, {"claude"}, false);
        QVERIFY(!choice.local());
        QCOMPARE(provider, QString("claude"));
        provider = choice.update(provider, {"claude"}, false, true);
        QCOMPARE(provider, QString("local"));
        QCOMPARE(choice.update(provider, {"claude"}, false), QString("local"));
        provider = choice.update(provider, {"claude"}, false, false);
        QCOMPARE(provider, QString("claude"));
        WelcomeChoice reopened;
        QCOMPARE(reopened.update("local", {}, false), QString("local"));
        QCOMPARE(reopened.update("local", {"claude"}, false), QString("local"));
        WelcomeChoice automatic;
        QCOMPARE(automatic.update("claude", {}, false), QString("local"));
        automatic.providerChosen();
        QCOMPARE(automatic.update("local", {"claude"}, false), QString("local"));
        QVERIFY(!isSetupSignInProvider("local"));
        QVERIFY(!isSetupSignInProvider("endpoint"));
        QVERIFY(isSetupSignInProvider("codex"));
    }

    void interruptedDownloadsResumeButCancelledOnesDoNot()
    {
        QTemporaryDir directory;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QUrl origin(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        const auto &model = *findLocalModel("moonshine-small");
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        {
            LocalModelStore models(directory.path(), origin);
            LocalSetup setup(settings, providers, models);
            setup.download(model);
            QVERIFY(models.isDownloading(model.id));
            QVERIFY(setup.modelState(model).downloading);
            QVERIFY(!setup.modelState(model).downloaded);
        }
        {
            LocalModelStore models(directory.path(), origin);
            LocalSetup setup(settings, providers, models);
            QTRY_VERIFY(models.isDownloading(model.id));
            setup.cancelDownload(model.id);
        }
        LocalModelStore models(directory.path(), origin);
        LocalSetup setup(settings, providers, models);
        QCoreApplication::processEvents();
        QVERIFY(!models.isDownloading(model.id));
    }

    void speechModelChoiceAndDisplayAreShared()
    {
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        providers.registerSpeechProvider({"local", "Local model", {}},
            [&models](QObject *parent) { return new LocalSpeechTranscriber(models, parent); });
#endif
        LocalSetup setup(settings, providers, models);
        QVERIFY(!settings.localSpeechSettings().modelChosen);
        auto unchanged = settings.localSpeechSettings();
        unchanged.idleUnloadMinutes = 60;
        settings.setLocalSpeechSettings(unchanged);
        QVERIFY(!settings.localSpeechSettings().modelChosen);
        setup.initializeSpeechModel();
        QCOMPARE(settings.localSpeechSettings().modelId, setup.suggestedModel().id);
        QVERIFY(settings.localSpeechSettings().modelChosen);
        setup.chooseSpeechModel("cohere");
        setup.initializeSpeechModel();
        QCOMPARE(setup.speechModelChoice().id, QString("cohere"));
        const auto &model = *findLocalModel("cohere");
        auto speech = settings.snapshot().speech;
        auto state = setup.modelState(model, speech);
        QVERIFY(!state.downloaded && !state.downloading && !state.inUse);
        QCOMPARE(state.speedText, QString("Not measured"));
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        setup.probeHardware();
        QTRY_VERIFY(setup.hardwareKnown());
        const auto estimated = setup.modelState(*findLocalModel("parakeet"));
        QVERIFY(estimated.speedText.endsWith("(estimated)"));
#endif
        auto local = settings.localSpeechSettings();
        local.speedTestSeconds.insert(model.id, 0.8);
        settings.setLocalSpeechSettings(local);
        QCOMPARE(setup.modelState(model, speech).speedText, speechSecondsText(0.8));
        {
            QFile file(models.modelPath(model));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.resize(model.sizeBytes));
        }
        speech.providerId = "local";
        state = setup.modelState(model, speech);
        QVERIFY(state.downloaded && state.inUse);
        speech.providerId = "claude";
        QVERIFY(!setup.modelState(model, speech).inUse);
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        auto *provider = qobject_cast<LocalSpeechTranscriber *>(providers.speechProvider("local"));
        emit provider->speedTestFinished(model.id, 0, "speed problem");
        QCOMPARE(setup.modelState(model, speech).problem, QString("speed problem"));
#endif
        emit models.downloadFailed(model.id, "download problem");
        QCOMPARE(setup.modelState(model, speech).problem, QString("download problem"));
    }

    void catalogEntriesArePinned()
    {
        const QList<LocalModel> &models = localModelCatalog();
        QCOMPARE(models.size(), 4);
        for (const LocalModel &model : models) {
            QCOMPARE(model.revision.size(), 40);
            QCOMPARE(model.sha256.size(), 64);
            QVERIFY(model.sizeBytes > 0);
        }
        QVERIFY(!findLocalModel(QStringLiteral("granite")));
        QCOMPARE(findLocalModel(QStringLiteral("parakeet"))->sizeBytes, 731357568);
    }

    void fitComparesModelMemoryWithTheBudget()
    {
        // Parakeet needs 731 MB x 1.35 = 0.99 GB.
        const LocalModel &parakeet = *findLocalModel(QStringLiteral("parakeet"));
        HardwareProfile cpu;
        cpu.availableRamBytes = 5 * gb;      // budget 5 GB - 2 GiB = 2.85 GB
        QCOMPARE(modelFit(parakeet, cpu), ModelFit::Fits);
        cpu.availableRamBytes = 3 * gb;      // budget 0.85 GB
        QCOMPARE(modelFit(parakeet, cpu), ModelFit::TooLarge);
        cpu.availableRamBytes = 3.5 * gb;    // budget 1.35 GB
        QCOMPARE(modelFit(parakeet, cpu), ModelFit::Tight);

        // A dedicated GPU keeps 1 GiB back and ignores system RAM. Voxtral
        // Small needs 23.1 GB: too large for a 16 GB card, tight on a 24 GB one.
        const LocalModel &voxtral = *findLocalModel(QStringLiteral("voxtral-small"));
        HardwareProfile gpu;
        gpu.accelerator = HardwareProfile::Accelerator::DedicatedGpu;
        gpu.availableRamBytes = 64 * gb;
        gpu.gpuMemoryBytes = 16 * gib;
        QCOMPARE(modelFit(voxtral, gpu), ModelFit::TooLarge);
        gpu.gpuMemoryBytes = 24 * gib;
        QCOMPARE(modelFit(voxtral, gpu), ModelFit::Tight);

        // A 36 GB Mac: min(28 GB, 0.85 x (32.4 GB - 2.68 GB)) = 25.26 GB.
        QVERIFY(qAbs(double(memoryBudgetBytes(m4Max36GB())) - 25.258e9) < 0.01e9);
        QCOMPARE(modelFit(voxtral, m4Max36GB()), ModelFit::Tight);
    }

    void speedIsEstimatedFromTheNearestReferenceMachine()
    {
        const LocalModel &parakeet = *findLocalModel(QStringLiteral("parakeet"));
        const std::optional<SpeedEstimate> laptop = estimatedSpeed(parakeet, laptop4750u());
        QVERIFY(laptop);
        QCOMPARE(laptop->secondsFor10sSpeech, 10.0 / 26.01);
        QVERIFY(laptop->measuredOnThisChip);

        HardwareProfile otherPc = laptop4750u();
        otherPc.chipName = QStringLiteral("Intel Core i5-6200U");
        QVERIFY(!estimatedSpeed(parakeet, otherPc)->measuredOnThisChip);

        QCOMPARE(estimatedSpeed(parakeet, m4Max36GB())->secondsFor10sSpeech, 10.0 / 228.39);
        // transcribe.cpp never ran Voxtral Small on the 4750U.
        QVERIFY(!estimatedSpeed(*findLocalModel(QStringLiteral("voxtral-small")), otherPc));
    }

    void suggestionPrefersTheMostAccurateQuickStreamingModelThatFits()
    {
        // Values from the approved prototype's four machines.
        QCOMPARE(suggestedLocalModel(laptop4750u()).id, QStringLiteral("parakeet"));
        QCOMPARE(suggestedLocalModel(m4Max36GB()).id, QStringLiteral("parakeet"));

        // 4 GB, no GPU, little of it free: nothing fits, so the smallest model.
        HardwareProfile old;
        old.chipName = QStringLiteral("Intel Core i5-6200U");
        old.systemRamBytes = 4 * gb;
        old.availableRamBytes = 2.2 * gb;
        QCOMPARE(modelFit(*findLocalModel(QStringLiteral("moonshine-small")), old), ModelFit::TooLarge);
        QCOMPARE(suggestedLocalModel(old).id, QStringLiteral("moonshine-small"));

        // Parakeet fits only tightly, Moonshine fits: Moonshine.
        HardwareProfile small;
        small.availableRamBytes = 3.8 * gb;
        QCOMPARE(modelFit(*findLocalModel(QStringLiteral("parakeet")), small), ModelFit::Tight);
        QCOMPARE(suggestedLocalModel(small).id, QStringLiteral("moonshine-small"));
    }

    void downloadResumesAPartialFileAndVerifiesIt()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content = QByteArray("GGUF") + QByteArray(4096, 'x') + "end";
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        {
            QFile part(store.modelPath(model) + QStringLiteral(".part"));
            QVERIFY(part.open(QIODevice::WriteOnly));
            part.write(content.left(1000));
        }
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        QVERIFY(store.isDownloading(model.id));
        const QByteArray request = serveOnce(server, "206 Partial Content", content.mid(1000));

        QVERIFY(request.startsWith("GET /owner/repo/resolve/abc123/fake.gguf "));
        QVERIFY(request.contains("\r\nRange: bytes=1000-\r\n"));
        QVERIFY(finished.wait(5000));
        QCOMPARE(failed.size(), 0);
        QVERIFY(store.isDownloaded(model));
        QVERIFY(!store.isDownloading(model.id));
        QFile file(store.modelPath(model));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        QVERIFY(!QFile::exists(store.modelPath(model) + QStringLiteral(".part")));

        QVERIFY(store.remove(model));
        QVERIFY(!QFile::exists(store.modelPath(model)));
    }

    void downloadWithTheWrongHashIsDeleted()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content(2048, 'a');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        serveOnce(server, "200 OK", QByteArray(2048, 'b'));

        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), model.id);
        QVERIFY(failed.first().at(1).toString().contains(QStringLiteral("damaged")));
        QCOMPARE(finished.size(), 0);
        QVERIFY(!store.isDownloaded(model));
        QVERIFY(!QFile::exists(store.modelPath(model)));
        QVERIFY(!QFile::exists(store.modelPath(model) + QStringLiteral(".part")));
    }

    void interruptedDownloadKeepsWhatArrivedForTheNextTry()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content(4000, 'c');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        // The server promises the whole file and hangs up after 1500 bytes.
        serveOnce(server, "200 OK", content.left(1500), content.size());

        QVERIFY(failed.wait(5000));
        QCOMPARE(QFileInfo(store.modelPath(model) + QStringLiteral(".part")).size(), 1500);
        QVERIFY(!store.isDownloading(model.id));
    }

    void cancelKeepsThePartialFile()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content(4000, 'd');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        QSignalSpy progress(&store, &LocalModelStore::downloadProgress);

        store.download(model);
        QVERIFY(server.waitForNewConnection(5000));
        QTcpSocket *socket = server.nextPendingConnection();
        test::readHttpRequest(socket, 2000);
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 4000\r\n\r\n" + content.left(1200));
        socket->flush();
        QTRY_VERIFY(!progress.isEmpty());
        store.cancel(model.id);

        QVERIFY(!store.isDownloading(model.id));
        QCOMPARE(QFileInfo(store.modelPath(model) + QStringLiteral(".part")).size(), 1200);
    }

    void serverIgnoringTheRangeRestartsFromZero()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content = QByteArray(3000, 'e') + "tail";
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        {
            QFile part(store.modelPath(model) + QStringLiteral(".part"));
            QVERIFY(part.open(QIODevice::WriteOnly));
            part.write(QByteArray(1000, 'z'));
        }
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);

        store.download(model);
        serveOnce(server, "200 OK", content);

        QVERIFY(finished.wait(5000));
        QFile file(store.modelPath(model));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
    }

    void rangeNotSatisfiableRestartsTheDownload()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray content(2500, 'f');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        {
            QFile part(store.modelPath(model) + QStringLiteral(".part"));
            QVERIFY(part.open(QIODevice::WriteOnly));
            part.write(QByteArray(1000, 'z'));
        }
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);

        store.download(model);
        QVERIFY(serveOnce(server, "416 Range Not Satisfiable", {}).contains("\r\nRange: bytes=1000-\r\n"));
        // The retry comes from the event loop, which serveOnce does not run.
        QTRY_VERIFY(server.hasPendingConnections());
        const QByteArray retry = serveOnce(server, "200 OK", content);

        QVERIFY(retry.startsWith("GET "));
        QVERIFY(!retry.contains("Range:"));
        QVERIFY(finished.wait(5000));
        QVERIFY(store.isDownloaded(model));
    }

    void cancelDoesNotWaitForAnotherModelsHashCheck()
    {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        LocalModelStore store(dir.path(), QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())));
        // A complete, sparse 512 MiB partial file goes straight to the hash
        // check. Its hash is wrong, which the check only learns at the end.
        LocalModel large = fakeModel({});
        large.id = QStringLiteral("large");
        large.fileName = QStringLiteral("large.gguf");
        large.sizeBytes = qint64(512) << 20;
        {
            QFile part(store.modelPath(large) + QStringLiteral(".part"));
            QVERIFY(part.open(QIODevice::WriteOnly));
            QVERIFY(part.resize(large.sizeBytes));
        }
        const LocalModel other = fakeModel("other");
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        QElapsedTimer timer;
        timer.start();
        store.download(large);
        store.download(other);
        store.cancel(other.id);
        const qint64 cancelMs = timer.elapsed();
        QVERIFY(failed.wait(60000));
        const qint64 hashMs = timer.elapsed();

        QCOMPARE(failed.first().at(0).toString(), large.id);
        QVERIFY2(cancelMs * 4 < hashMs, qPrintable(QStringLiteral("cancel took %1 ms of a %2 ms hash check")
                                                       .arg(cancelMs).arg(hashMs)));
    }

#ifdef SPEECHER_WITH_LOCAL_SPEECH
    // Runs a real model when SPEECHER_TEST_LOCAL_MODEL names a catalog id and
    // SPEECHER_TEST_LOCAL_MODEL_DIR holds its file.
    void localModelTranscribesTheSpeedTestClip()
    {
        const QString modelId = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL");
        const QString modelDir = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL_DIR");
        if (modelId.isEmpty() || modelDir.isEmpty()) {
            QSKIP("Set SPEECHER_TEST_LOCAL_MODEL and SPEECHER_TEST_LOCAL_MODEL_DIR to run a real model.");
        }
        const LocalModel *model = findLocalModel(modelId);
        QVERIFY(model);
        LocalModelStore store(modelDir, QUrl());
        QVERIFY2(store.isDownloaded(*model), qPrintable(store.modelPath(*model)));
        LocalSpeechTranscriber transcriber(store);
        QSignalSpy partials(&transcriber, &SpeechTranscriber::partialTranscript);
        QSignalSpy transcripts(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);

        SpeechSettings settings;
        settings.local.modelId = modelId;
        QVERIFY(transcriber.prepare(settings).ok);
        QFile clip(QStringLiteral(":/speedtest/librispeech-6930-75918-0018.s16le"));
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray pcm = clip.readAll();

        transcriber.startAttempt(1, settings);
        // 100 ms chunks at the pace the microphone delivers them.
        constexpr qsizetype chunkBytes = 3200;
        for (qsizetype offset = 0; offset < pcm.size(); offset += chunkBytes) {
            transcriber.sendAudio(1, pcm.mid(offset, chunkBytes));
            QTest::qWait(100);
        }
        QElapsedTimer afterStop;
        afterStop.start();
        transcriber.finishInput(1);
        QVERIFY(completed.wait(120000));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(transcripts.size(), 1);
        const QString text = transcripts.first().at(1).toString();
        qInfo().noquote() << modelId << "transcript:" << text;
        qInfo() << "partials" << partials.size() << "; transcript" << afterStop.elapsed()
                << "ms after the last of" << pcm.size() / 32 << "ms of audio";
        QVERIFY(text.contains(QStringLiteral("tempest"), Qt::CaseInsensitive));
        QVERIFY(text.contains(QStringLiteral("security everywhere"), Qt::CaseInsensitive));

        QSignalSpy speed(&transcriber, &LocalSpeechTranscriber::speedTestFinished);
        transcriber.runSpeedTest(modelId, {});
        QVERIFY(speed.wait(120000));
        QVERIFY2(speed.first().at(2).toString().isEmpty(), qPrintable(speed.first().at(2).toString()));
        qInfo() << "speed test:" << speed.first().at(1).toDouble() << "s for 10 s of speech";
        QVERIFY(speed.first().at(1).toDouble() > 0);
    }

    // An attempt that starts while the worker is busy must still get every
    // chunk, including the ones sent before its begin() runs. The Speed Test
    // keeps the worker busy so the calls below all queue behind it.
    void attemptStartedWhileTheWorkerIsBusyKeepsItsAudio()
    {
        const QString modelId = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL");
        const QString modelDir = qEnvironmentVariable("SPEECHER_TEST_LOCAL_MODEL_DIR");
        if (modelId.isEmpty() || modelDir.isEmpty()) {
            QSKIP("Set SPEECHER_TEST_LOCAL_MODEL and SPEECHER_TEST_LOCAL_MODEL_DIR to run a real model.");
        }
        LocalModelStore store(modelDir, QUrl());
        LocalSpeechTranscriber transcriber(store);
        QSignalSpy transcripts(&transcriber, &SpeechTranscriber::attemptTranscript);
        QSignalSpy completed(&transcriber, &SpeechTranscriber::attemptCompleted);
        SpeechSettings settings;
        settings.local.modelId = modelId;
        QFile clip(QStringLiteral(":/speedtest/librispeech-6930-75918-0018.s16le"));
        QVERIFY(clip.open(QIODevice::ReadOnly));
        const QByteArray pcm = clip.readAll();

        transcriber.runSpeedTest(modelId, {});
        transcriber.startAttempt(1, settings);
        transcriber.sendAudio(1, pcm.left(3200));
        transcriber.cancelAttempt(1);
        transcriber.startAttempt(2, settings);
        transcriber.sendAudio(2, pcm);
        transcriber.finishInput(2);

        QVERIFY(completed.wait(120000));
        QCOMPARE(completed.first().at(0).toULongLong(), quint64(2));
        QVERIFY2(transcripts.first().at(1).toString().contains(QStringLiteral("security everywhere"),
                                                                Qt::CaseInsensitive),
                 qPrintable(transcripts.first().at(1).toString()));
    }

    void streamSplitKeepsPartWordsTentative()
    {
        // Mid-word commit: "wor" is not a whole word yet.
        StreamSplit split = splitStreamText({QStringLiteral("hello wor"), QStringLiteral("ld again")}, 0);
        QCOMPARE(split.finalWords, QStringLiteral("hello"));
        QCOMPARE(split.partial, QStringLiteral(" world again"));
        QCOMPARE(split.finalChars, 5);

        // The committed word is complete once a space follows it.
        split = splitStreamText({QStringLiteral("hello world "), QStringLiteral("again")}, 5);
        QCOMPARE(split.finalWords, QStringLiteral(" world"));
        QCOMPARE(split.partial, QStringLiteral(" again"));
        QCOMPARE(split.finalChars, 11);

        // Committed text that grows only inside the last word adds no final.
        split = splitStreamText({QStringLiteral("hello world aga"), QStringLiteral("in")}, 11);
        QVERIFY(split.finalWords.isEmpty());
        QCOMPARE(split.partial, QStringLiteral(" again"));
        QCOMPARE(split.finalChars, 11);

        split = splitStreamText({}, 0);
        QVERIFY(split.finalWords.isEmpty());
        QVERIFY(split.partial.isEmpty());
        QCOMPARE(split.finalChars, 0);

        // Nothing committed yet: all of it is tentative.
        split = splitStreamText({{}, QStringLiteral("the night")}, 0);
        QVERIFY(split.finalWords.isEmpty());
        QCOMPARE(split.partial, QStringLiteral("the night"));
    }

    void missingModelFailsWithAnActionableMessage()
    {
        QTemporaryDir dir;
        LocalModelStore store(dir.path(), QUrl());
        LocalSpeechTranscriber transcriber(store);
        QSignalSpy failed(&transcriber, &SpeechTranscriber::failed);
        SpeechSettings settings;

        QVERIFY(!transcriber.prepare(settings).ok);
        transcriber.startAttempt(7, settings);
        QVERIFY(failed.wait(2000));
        const SpeechFailure failure = failed.first().at(0).value<SpeechFailure>();
        QCOMPARE(failure.attemptId, quint64(7));
        QVERIFY(failure.message.contains(QStringLiteral("Local models page")));
    }
#endif
};

} // namespace

int runLocalModelsTests(int argc, char **argv)
{
    LocalModelsTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_local_models.moc"
