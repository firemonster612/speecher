#include "app/LocalSetup.h"
#include "app/SetupSteps.h"
#include "core/SettingsStore.h"
#include "core/SecretStore.h"
#include "core/settings/SettingsKeys.h"
#include "core/settings/SettingsSchema.h"
#include "providers/ProviderRegistry.h"
#include "common/test_http.h"
#include "common/test_suites.h"
#include "core/LocalModelCatalog.h"
#include "core/TranscriptState.h"
#include "providers/LocalModelStore.h"
#ifdef SPEECHER_WITH_LOCAL_SPEECH
#include "providers/LocalSpeechTranscriber.h"
#endif

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <algorithm>

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

} // namespace

namespace speecher {
class LocalSetupTestAccess {
public:
    static void setHardware(LocalSetup &setup, const HardwareProfile &profile)
    {
        setup.m_hardware.profile = profile;
        setup.m_hardwareKnown = true;
    }
    static void setHardware(LocalSetup &setup, const HardwareSummary &hardware)
    {
        setup.m_hardware = hardware;
        setup.m_hardwareKnown = true;
    }
};
} // namespace speecher

namespace {

// Header names are case-insensitive, and Qt on Windows sends them lowercase.
bool asksForRange(const QByteArray &request, const QByteArray &range)
{
    return request.toLower().contains("\r\nrange: " + range + "\r\n");
}

QUrl listenLocally(QTcpServer &server)
{
    if (!server.listen(QHostAddress::LocalHost)) qFatal("cannot listen on localhost");
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
}

void writePart(const LocalModelStore &store, const LocalModel &model, const QByteArray &bytes)
{
    QFile part(store.modelPath(model) + QStringLiteral(".part"));
    if (!part.open(QIODevice::WriteOnly) || part.write(bytes) != bytes.size()) qFatal("cannot write partial file");
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
    void liveFactsDoesNotReadUnreadKeyringSecrets()
    {
        SettingsStore settings;
        settings.raw().clear();
        settings.raw().setValue(SettingsKeys::SecretsInKeyring,
                               QStringList{QStringLiteral("speech-endpoint-key")});
        qputenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SPEECHER_TEST_KEYRING_READ_TIMEOUT"); });
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);

        QVERIFY(!setup.liveFacts().modelFolder.isEmpty());
        QVERIFY2(settings.secrets()->lastError().isEmpty(), "liveFacts tried to read the keyring");
        QVERIFY(!settings.secrets()->isSecretKnown(SecretStore::Secret::SpeechEndpointKey));
    }

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
        // Picking a model keeps the verdict; another server does not.
        snapshot.speech.endpoint.model = "picked";
        snapshot.refinement.endpoint.model = "picked";
        settings.applySnapshot(snapshot);
        QCOMPARE(modelsForCheck(), QStringList{"new-B"});
        snapshot.speech.endpoint = endpoint(slow.serverPort());
        snapshot.refinement.endpoint.baseUrl = snapshot.speech.endpoint.baseUrl;
        settings.applySnapshot(snapshot);
        const auto facts = setup.liveFacts();
        QVERIFY((refinement ? facts.refinementEndpointStatus : facts.speechEndpointStatus).isEmpty());
        QVERIFY(modelsForCheck().isEmpty());
    }

    void aSavedEndpointIsCheckedOnceEditsPauseAndGetsTheFirstModel()
    {
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        QTemporaryDir directory;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto snapshot = settings.snapshot();
        snapshot.refinement.providerId = "endpoint";
        snapshot.refinement.endpoint.baseUrl = "http://127.0.0.1:1/v";
        settings.applySnapshot(snapshot);
        // Still typing: the address grows before the pause.
        snapshot.refinement.endpoint.baseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        settings.applySnapshot(snapshot);
        QVERIFY(setup.liveFacts().refinementEndpointStatus.isEmpty());

        QTRY_VERIFY(server.hasPendingConnections());
        QVERIFY(serveOnce(server, "200 OK", R"({"data":[{"id":"first"},{"id":"second"}]})")
                    .startsWith("GET /v1/models"));
        QTRY_COMPARE(settings.refinementEndpointSettings().model, QString("first"));
        QCOMPARE(setup.liveFacts().refinementEndpointStatus, QString("Connected. Models available: 2."));
        QCOMPARE(setup.liveFacts().refinementEndpointModels, QStringList({"first", "second"}));
        QVERIFY(!server.waitForNewConnection(1000));
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
        // A pull is saved under the name Ollama lists it by.
        QCOMPARE(ollamaListedName("gemma4:e4b"), QString("gemma4:e4b"));
        QCOMPARE(ollamaListedName("LiquidAI/lfm2.5-1.2b-instruct"), QString("LiquidAI/lfm2.5-1.2b-instruct:latest"));
        QCOMPARE(ownModelRefinementSummary({.providerId = "local", .localRunner = {"lmstudio", "m"}}),
                 QString("LM Studio with m"));
    }

    void setupStepsEndOnReadyAfterTheShortcut()
    {
        QStringList ids;
        for (const SetupStepInfo &step : setupSteps()) ids.append(step.id);
        QCOMPARE(ids.last(), QString("ready"));
        QVERIFY(ids.indexOf("shortcut") < ids.indexOf("ready"));
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        QCOMPARE(ids.indexOf("login"), ids.size() - 2);
#endif
#ifdef Q_OS_WIN
        const bool accessibilityStep = false;
#else
        const bool accessibilityStep = true;
#endif
        QCOMPARE(ids.contains("accessibility"), accessibilityStep);
        QCOMPARE(findSetupStep("shortcut")->title, QString("Global Shortcut"));
        QCOMPARE(setupChecklistLine("delivery", "clipboard"), QString("Text delivery — clipboard"));
        QCOMPARE(setupProviderVerdict("codex", false), QString("Not signed in"));
        QCOMPARE(setupProviderVerdict("endpoint", false), QString("Not set up"));
        QCOMPARE(setupRefinementStatus("none", std::nullopt), QString("No cleanup"));
        QCOMPARE(setupSchemaRow("defaultWritingProfile").label, QString("Fallback profile"));
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
        const auto beforeProbe = settings.localSpeechSettings().modelId;
        setup.initializeSpeechModel();
        QVERIFY(!settings.localSpeechSettings().modelChosen);
        QCOMPARE(settings.localSpeechSettings().modelId, beforeProbe);
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        setup.probeHardware();
        QTRY_VERIFY(setup.hardwareKnown());
        QCOMPARE(settings.localSpeechSettings().modelId, setup.suggestedModel().id);
        QCOMPARE(setup.speechModelChoice().id, setup.suggestedModel().id);
        QVERIFY(!settings.localSpeechSettings().modelChosen);
#endif
        setup.chooseSpeechModel("cohere");
        QVERIFY(settings.localSpeechSettings().modelChosen);
        setup.initializeSpeechModel();
        QCOMPARE(setup.speechModelChoice().id, QString("cohere"));
        const auto &model = *findLocalModel("cohere");
        auto speech = settings.snapshot().speech;
        auto state = setup.modelState(model, speech);
        QVERIFY(!state.downloaded && !state.downloading && !state.inUse);
        // A result belongs to where it ran, so switching Runs on leaves the
        // model unmeasured until it is tested there.
        auto local = settings.localSpeechSettings();
        local.speedTestSeconds.insert(localSpeedTestKey(model.id, {QStringLiteral("cpu"), {}}), 0.8);
        settings.setLocalSpeechSettings(local);
        QCOMPARE(setup.measuredSeconds(model.id), std::nullopt);
        local.runsOn = {QStringLiteral("cpu"), {}};
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

    void modelStateDescribesTheCardAndTableRow()
    {
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        const LocalModel &moonshine = *findLocalModel("moonshine-small");
        const LocalModel &parakeet = *findLocalModel("parakeet");
        const LocalModel &voxtral = *findLocalModel("voxtral-small");

        auto state = setup.modelState(moonshine);
        QVERIFY(!state.suggested);
        QVERIFY(!state.tooLarge);
        QCOMPARE(state.tableCells, QStringList({"Moonshine Small", "199 MB", "2.54% / 8.55%", "Not measured",
                                                "As you speak", "Checking…"}));

        LocalSetupTestAccess::setHardware(setup, laptop4750u());
        state = setup.modelState(parakeet);
        QVERIFY(state.suggested);
        QVERIFY(!state.tooLarge);
        QCOMPARE(state.cardFacts, QString("Words appear as you speak\n"
                                          "About 0.4 s per 10 s (measured on the same chip)\n"
                                          "1.6% of words wrong on clear speech, 3.99% on everyday speech"));
        QCOMPARE(state.tableCells, QStringList({"Parakeet 0.6B (suggested)", "731 MB", "1.6% / 3.99%",
                                                "~0.4 s (estimated)", "As you speak", "Fits"}));
        QVERIFY(!setup.modelState(moonshine).suggested);
        state = setup.modelState(voxtral);
        QVERIFY(state.tooLarge);
        QCOMPARE(state.tableCells.last(), QString("Too large"));
    }

    void fitFollowsWhereModelsRun()
    {
        // A 2 GiB card and 16 GB of free RAM: Qwen3-ASR's 2.95 GB is too
        // large for the card, but fits on the CPU.
        HardwareSummary hardware;
        hardware.profile.accelerator = HardwareProfile::Accelerator::DedicatedGpu;
        hardware.profile.availableRamBytes = 16 * gb;
        hardware.profile.gpuMemoryBytes = 2 * gib;
        LocalSpeechEngine::Device card;
        card.id = QStringLiteral("0000:01:00.0");
        card.kind = QStringLiteral("vulkan");
        card.type = LocalSpeechEngine::Device::Type::Gpu;
        card.memoryTotalBytes = 2 * gib;
        hardware.gpus = {card};
        const LocalModel &qwen = *findLocalModel(QStringLiteral("qwen3-asr"));

        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {})), ModelFit::TooLarge);
        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {QStringLiteral("cpu"), {}})), ModelFit::Fits);
        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {QStringLiteral("vulkan"), card.id})), ModelFit::TooLarge);
        // A card saved before backends could be chosen is still that card.
        QCOMPARE(modelFit(qwen, runsOnProfile(hardware, {QStringLiteral("auto"), card.id})), ModelFit::TooLarge);

        // LocalSetup re-reads the choice, so the page follows a change.
        QTemporaryDir directory;
        SettingsStore settings;
        settings.raw().clear();
        ProviderRegistry providers;
        LocalModelStore models(directory.path(), QUrl("http://127.0.0.1:1"));
        LocalSetup setup(settings, providers, models);
        LocalSetupTestAccess::setHardware(setup, hardware);
        QVERIFY(setup.modelState(qwen).tooLarge);
        LocalSpeechSettings local = settings.localSpeechSettings();
        local.runsOn = {QStringLiteral("cpu"), {}};
        settings.setLocalSpeechSettings(local);
        QVERIFY(!setup.modelState(qwen).tooLarge);
        QCOMPARE(setup.fitLabel(qwen), QStringLiteral("Fits"));
    }

    void catalogEntriesArePinned()
    {
        for (const LocalModel &model : localModelCatalog()) {
            QCOMPARE(model.revision.size(), 40);
            QCOMPARE(model.sha256.size(), 64);
            QVERIFY(model.sizeBytes > 0);
        }
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

        // Parakeet fits only tightly, both Moonshines fit: the more accurate one.
        HardwareProfile small;
        small.availableRamBytes = 3.8 * gb;
        QCOMPARE(modelFit(*findLocalModel(QStringLiteral("parakeet")), small), ModelFit::Tight);
        QCOMPARE(suggestedLocalModel(small).id, QStringLiteral("moonshine-medium"));
    }

    void downloadResumesAPartialFileAndVerifiesIt()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content = QByteArray("GGUF") + QByteArray(4096, 'x') + "end";
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        writePart(store, model, content.left(1000));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);
        QSignalSpy failed(&store, &LocalModelStore::downloadFailed);

        store.download(model);
        QVERIFY(store.isDownloading(model.id));
        const QByteArray request = serveOnce(server, "206 Partial Content", content.mid(1000));

        QVERIFY(request.startsWith("GET /owner/repo/resolve/abc123/fake.gguf "));
        QVERIFY(asksForRange(request, "bytes=1000-"));
        QVERIFY(finished.wait(5000));
        QCOMPARE(failed.size(), 0);
        QVERIFY(store.isDownloaded(model));
        QVERIFY(!store.isDownloading(model.id));
        QFile file(store.modelPath(model));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        // Windows will not delete a file something still holds open.
        file.close();
        QVERIFY(!QFile::exists(store.modelPath(model) + QStringLiteral(".part")));

        QVERIFY(store.remove(model));
        QVERIFY(!QFile::exists(store.modelPath(model)));
    }

    void downloadWithTheWrongHashIsDeleted()
    {
        QTemporaryDir dir;
        QTcpServer server;
        const QByteArray content(2048, 'a');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
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
        const QByteArray content(4000, 'c');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
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
        const QByteArray content(4000, 'd');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
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
        const QByteArray content = QByteArray(3000, 'e') + "tail";
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        writePart(store, model, QByteArray(1000, 'z'));
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
        const QByteArray content(2500, 'f');
        const LocalModel model = fakeModel(content);
        LocalModelStore store(dir.path(), listenLocally(server));
        writePart(store, model, QByteArray(1000, 'z'));
        QSignalSpy finished(&store, &LocalModelStore::downloadFinished);

        store.download(model);
        QVERIFY(asksForRange(serveOnce(server, "416 Range Not Satisfiable", {}), "bytes=1000-"));
        // The retry comes from the event loop, which serveOnce does not run.
        QTRY_VERIFY(server.hasPendingConnections());
        const QByteArray retry = serveOnce(server, "200 OK", content);

        QVERIFY(retry.startsWith("GET "));
        QVERIFY(!retry.toLower().contains("\r\nrange:"));
        QVERIFY(finished.wait(5000));
        QVERIFY(store.isDownloaded(model));
    }

    void cancelDoesNotWaitForAnotherModelsHashCheck()
    {
        QTemporaryDir dir;
        QTcpServer server;
        LocalModelStore store(dir.path(), listenLocally(server));
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
        QSignalSpy runsOn(&transcriber, &LocalSpeechTranscriber::runsOnChanged);

        SpeechSettings settings;
        settings.local.modelId = modelId;
        // Local models get no key terms. Given to Whisper as a prompt, terms
        // like these replaced whole clips with other text.
        settings.vocabulary = {QStringLiteral("Speecher"), QStringLiteral("Kirigami")};
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
        // SPEECHER_TEST_LOCAL_RUNS_ON picks where the Speed Test runs, as the
        // Runs on row stores it: "cpu", "vulkan", "cuda:0000:01:00.0".
        transcriber.runSpeedTest(modelId, localRunsOnFromId(
            qEnvironmentVariable("SPEECHER_TEST_LOCAL_RUNS_ON", QStringLiteral("auto"))));
        QVERIFY(speed.wait(120000));
        QVERIFY2(speed.first().at(2).toString().isEmpty(), qPrintable(speed.first().at(2).toString()));
        if (!runsOn.isEmpty()) {
            qInfo().noquote() << "runs on:" << runsOn.last().at(0).toString();
        }
        qInfo() << "speed test:" << speed.first().at(1).toDouble() << "s for 10 s of speech";
        QVERIFY(speed.first().at(1).toDouble() > 0);
    }

    // An explicit backend that is missing fails the load rather than running
    // somewhere else. No model file is needed to get that far.
    void anUnavailableBackendFailsTheLoad()
    {
        const QList<LocalSpeechEngine::Device> devices = LocalSpeechEngine::devices();
        if (std::any_of(devices.cbegin(), devices.cend(), [](const LocalSpeechEngine::Device &device) {
                return device.kind == QStringLiteral("rocm");
            })) {
            QSKIP("This computer runs ROCm.");
        }
        LocalSpeechEngine engine([] { return false; });
        QString error;
        QVERIFY(!engine.load(QStringLiteral("/nonexistent.gguf"), {QStringLiteral("rocm"), {}}, &error));
        QCOMPARE(error, QStringLiteral("ROCm is not available on this computer. Choose where the model "
                                       "runs on the Local models page."));
        QVERIFY(engine.runsOnDescription().isEmpty());
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

        // Nothing committed yet: all of it is tentative.
        split = splitStreamText({{}, QStringLiteral("the night")}, 0);
        QVERIFY(split.finalWords.isEmpty());
        QCOMPARE(split.partial, QStringLiteral("the night"));
    }

    void shorterRawFinalDoesNotDiscardCommittedStreamText()
    {
        const QString committed = QStringLiteral(
            "The night was clear, starlit and splendid. The tempest had passed away, "
            "and the sweet influences of the evening had restored life, peace and security everywhere.");
        const QString truncated = QStringLiteral(
            "The night was clear, starlit and splendid. The tempest had passed away.");
        TranscriptState transcript;
        const StreamSplit split = splitStreamText({committed, {}}, 0);
        transcript.commitFinal(split.finalWords);
        transcript.setPartial(split.partial);
        QCOMPARE(transcript.text(), committed);

        // Finalization must not replace the already committed sentence with
        // a shorter raw model hypothesis.
        transcript.replaceFinals(finalStreamText(committed, truncated));
        QCOMPARE(transcript.text(), committed);

        const QString revised = QStringLiteral(
            "The night was clear, starlit, and splendid. The tempest had passed away, "
            "and the sweet influences of the evening had restored life, peace, and security everywhere.");
        QCOMPARE(finalStreamText(committed, revised), revised);
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
